/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/vfio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"
#include "vfio_pci.h"

#define VFIO_MAX_GROUPS    8
#define VFIO_GROUP_FMT     "/dev/vfio/%u"
#define SYSFS_PCI_DEV_PATH "/sys/bus/pci/devices"
#define MAX_REGION_INDEX   5

struct vfio_group {
	int group_num;
	int group_fd;
	int devices;
};

struct vfio_config {
	int container_fd;
	int active_groups;
	struct vfio_group groups[VFIO_MAX_GROUPS];
};

static struct vfio_config vfio_cfg = {.container_fd = -1};

static int
vfio_pci_init(void)
{
	int i;

	if (vfio_cfg.container_fd != -1)
		return 0;

	vfio_cfg.container_fd = open("/dev/vfio/vfio", O_RDWR);
	if (vfio_cfg.container_fd < 0) {
		log_write(LOG_ERR, "Failed to open VFIO file descriptor\n");
		return -1;
	}

	vfio_cfg.active_groups = 0;
	for (i = 0; i < VFIO_MAX_GROUPS; i++) {
		vfio_cfg.groups[i].group_num = -1;
		vfio_cfg.groups[i].group_fd = -1;
		vfio_cfg.groups[i].devices = 0;
	}

	return 0;
}

static int
vfio_pci_interrupt_init(struct vfio_pci_device *pdev)
{
	struct vfio_irq_info irq_info = {.argsz = sizeof(irq_info)};
	int rc;

	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;
	rc = ioctl(pdev->device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info);
	if (rc) {
		log_write(LOG_ERR, "%s: failed to get irq info, %s\n", pdev->name, strerror(errno));
		return -1;
	}

	if (!irq_info.flags & VFIO_IRQ_INFO_EVENTFD)
		return -1;

	if (!irq_info.count) {
		log_write(LOG_DEBUG, "%s: no msix vectors available\n", pdev->name);
		return -1;
	}

	pdev->intr.efds = calloc(irq_info.count, sizeof(int32_t));
	if (!pdev->intr.efds) {
		log_write(LOG_ERR, "%s: failed to allocate memory for eventfds\n", pdev->name);
		return -1;
	}

	/* All interrupts are disabled by default */
	memset(pdev->intr.efds, -1, irq_info.count * sizeof(int32_t));

	pdev->intr.count = irq_info.count;

	pthread_mutex_init(&pdev->intr.lock, NULL);
	return 0;
}

static int
vfio_get_group_num(const char *dev_name, int *group_num)
{
	char linkname[PATH_MAX], filename[PATH_MAX];
	char *tok, *group_tok;
	int rc;

	memset(linkname, 0, sizeof(linkname));
	memset(filename, 0, sizeof(filename));

	snprintf(linkname, sizeof(linkname), "%s/%s/iommu_group", SYSFS_PCI_DEV_PATH, dev_name);
	rc = readlink(linkname, filename, sizeof(filename));
	if (rc < 0)
		return -1;

	/* IOMMU group is always the last token */
	tok = strtok(filename, "/");
	if (!tok) {
		log_write(LOG_ERR, "Token not found\n");
		return -1;
	}

	group_tok = tok;
	while (tok) {
		group_tok = tok;
		tok = strtok(NULL, "/");
	}

	*group_num = strtol(group_tok, NULL, 10);

	return 0;
}

static int
vfio_get_group_fd(const char *dev_name)
{
	int group_fd, group_num, i, rc;
	char filename[PATH_MAX];

	rc = vfio_get_group_num(dev_name, &group_num);
	if (rc < 0) {
		log_write(LOG_ERR, "%s: Failed to get group number\n", dev_name);
		return -1;
	}

	for (i = 0; i < VFIO_MAX_GROUPS; i++) {
		if (vfio_cfg.groups[i].group_num == group_num) {
			vfio_cfg.groups[i].devices++;
			return vfio_cfg.groups[i].group_fd;
		}
	}

	snprintf(filename, sizeof(filename), VFIO_GROUP_FMT, group_num);
	group_fd = open(filename, O_RDWR);
	if (group_fd < 0) {
		log_write(LOG_ERR, "%s: failed to open %s\n", dev_name, filename);
		return -1;
	}

	for (i = 0; i < VFIO_MAX_GROUPS; i++) {
		if (vfio_cfg.groups[i].group_num == -1) {
			vfio_cfg.groups[i].group_num = group_num;
			vfio_cfg.groups[i].group_fd = group_fd;
			vfio_cfg.groups[i].devices = 1;
			vfio_cfg.active_groups++;
			return group_fd;
		}
	}

	log_write(LOG_ERR, "%s: Number of active groups surpasses the maximum supported limit\n",
		  dev_name);
	close(group_fd);

	return -1;
}

static void
vfio_pci_device_mem_free(struct vfio_pci_device *pdev)
{
	unsigned int i;

	for (i = 0; i < pdev->num_resource; i++)
		munmap(pdev->mem[i].addr, pdev->mem[i].len);

	free(pdev->mem);
}

static void
vfio_clear_group(int group_fd)
{
	int i;

	for (i = 0; i < VFIO_MAX_GROUPS; i++) {
		if (vfio_cfg.groups[i].group_fd == group_fd) {
			vfio_cfg.groups[i].devices--;
			if (!vfio_cfg.groups[i].devices) {
				close(group_fd);
				vfio_cfg.groups[i].group_num = -1;
				vfio_cfg.groups[i].group_fd = -1;
				vfio_cfg.active_groups--;
			}
		}
	}
}

int
vfio_pci_device_setup(struct vfio_pci_device *pdev)
{
	struct vfio_group_status group_status = {.argsz = sizeof(group_status)};
	struct vfio_device_info device_info = {.argsz = sizeof(device_info)};
	int group_fd, device_fd, rc;
	unsigned int i;

	if (vfio_pci_init())
		return -1;

	group_fd = vfio_get_group_fd(pdev->name);
	if (group_fd < 0)
		return -1;

	rc = ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status);
	if (rc < 0) {
		log_write(LOG_ERR, "%s: failed to get group status, %s\n", pdev->name,
			  strerror(errno));
		goto clear_group;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
		log_write(LOG_ERR,
			  "%s: VFIO group is not viable! "
			  "Not all devices in IOMMU group bound to VFIO or unbound\n",
			  pdev->name);
		goto clear_group;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET)) {
		if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &vfio_cfg.container_fd)) {
			log_write(LOG_ERR, "%s: failed to set VFIO container, %s\n", pdev->name,
				  strerror(errno));
			goto clear_group;
		}
	}

	if (vfio_cfg.active_groups == 1) {
		/* Configured only once after the assignment of the first group. */
		rc = ioctl(vfio_cfg.container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
		if (rc) {
			log_write(LOG_ERR, "%s: failed to set IOMMU type, %s\n", pdev->name,
				  strerror(errno));
			goto clear_group;
		}
	}

	device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, pdev->name);
	if (device_fd < 0) {
		log_write(LOG_ERR, "%s: failed to get device fd, %s\n", pdev->name,
			  strerror(errno));
		goto clear_group;
	}

	rc = ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info);
	if (rc) {
		log_write(LOG_ERR, "%s: failed to get device info, %s\n", pdev->name,
			  strerror(errno));
		goto close_device_fd;
	}

	pdev->device_fd = device_fd;
	pdev->group_fd = group_fd;
	pdev->mem = calloc(device_info.num_regions, sizeof(*pdev->mem));
	if (!pdev->mem) {
		log_write(LOG_ERR, "%s: failed to allocate memory for region info\n", pdev->name);
		goto close_device_fd;
	}

	for (i = 0; i < device_info.num_regions; i++) {
		struct vfio_region_info reg = {.argsz = sizeof(reg)};

		if (i > MAX_REGION_INDEX)
			break;

		reg.index = i;
		rc = ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &reg);
		if (rc) {
			log_write(LOG_ERR, "%s: failed to get region info, %s\n", pdev->name,
				  strerror(errno));
			goto device_mem_free;
		}

		if (!reg.size)
			continue;

		pdev->mem[pdev->num_resource].addr = mmap(NULL, reg.size, PROT_READ | PROT_WRITE,
							  MAP_SHARED, device_fd, reg.offset);
		if (pdev->mem[pdev->num_resource].addr == MAP_FAILED) {
			log_write(LOG_ERR, "%s: failed to mmap region %d\n", pdev->name, i);
			goto device_mem_free;
		}

		pdev->mem[pdev->num_resource].len = reg.size;
		pdev->mem[pdev->num_resource].index = i;

		log_write(LOG_DEBUG, "%s: Mapped region %d: addr=%p, len=%lu\n", pdev->name, i,
			  pdev->mem[pdev->num_resource].addr, pdev->mem[pdev->num_resource].len);
		pdev->num_resource++;
	}

	rc = vfio_pci_interrupt_init(pdev);
	if (rc) {
		log_write(LOG_ERR, "%s: failed to initialize interrupt\n", pdev->name);
		goto device_mem_free;
	}

	return 0;

device_mem_free:
	vfio_pci_device_mem_free(pdev);
close_device_fd:
	close(device_fd);
clear_group:
	vfio_clear_group(group_fd);
	return -1;
}

static int
vfio_pci_set_irqs(struct vfio_pci_device *pdev)
{
	struct vfio_irq_set *irq_set;
	size_t irq_set_size;
	int rc, *irq_data;
	uint32_t i;

	irq_set_size = sizeof(struct vfio_irq_set) + pdev->intr.count * sizeof(int);
	irq_set = calloc(1, irq_set_size);

	irq_set->argsz = irq_set_size;
	irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
	irq_set->start = 0;
	irq_set->count = pdev->intr.count;

	irq_data = (int *)(irq_set->data);
	for (i = 0; i < pdev->intr.count; i++)
		irq_data[i] = pdev->intr.efds[i];

	rc = ioctl(pdev->device_fd, VFIO_DEVICE_SET_IRQS, irq_set);
	if (rc)
		log_write(LOG_ERR, "%s: failed to set IRQs, %s\n", pdev->name, strerror(errno));

	free(irq_set);
	return rc;
}

int
vfio_pci_msix_enable(struct vfio_pci_device *pdev, uint32_t vec)
{
	int rc = -1;

	pthread_mutex_lock(&pdev->intr.lock);

	if (vec >= pdev->intr.count) {
		log_write(LOG_ERR, "%s: invalid vector %d\n", pdev->name, vec);
		goto exit;
	}

	if (pdev->intr.efds[vec] != -1) {
		log_write(LOG_ERR, "%s: vector %d already enabled\n", pdev->name, vec);
		goto exit;
	}

	pdev->intr.efds[vec] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (pdev->intr.efds[vec] < 0) {
		log_write(LOG_ERR, "%s: failed to create eventfd, %s\n", pdev->name,
			  strerror(errno));
		goto exit;
	}

	rc = vfio_pci_set_irqs(pdev);
exit:
	pthread_mutex_unlock(&pdev->intr.lock);
	return rc;
}

int
vfio_pci_msix_disable(struct vfio_pci_device *pdev, uint32_t vec)
{
	int rc = -1;

	pthread_mutex_lock(&pdev->intr.lock);

	if (vec >= pdev->intr.count) {
		log_write(LOG_ERR, "%s: invalid vector %d\n", pdev->name, vec);
		goto exit;
	}

	if (pdev->intr.efds[vec] == -1) {
		log_write(LOG_ERR, "%s: vector %d already disabled\n", pdev->name, vec);
		goto exit;
	}

	rc = close(pdev->intr.efds[vec]);
	if (rc) {
		log_write(LOG_ERR, "%s: failed to close eventfd, %s\n", pdev->name,
			  strerror(errno));
		goto exit;
	}

	pdev->intr.efds[vec] = -1;

	rc = vfio_pci_set_irqs(pdev);
exit:
	pthread_mutex_unlock(&pdev->intr.lock);
	return rc;
}

static void
vfio_pci_disable_interrupts(struct vfio_pci_device *pdev)
{
	struct vfio_irq_set irq_set = {.argsz = sizeof(irq_set)};
	uint32_t i;

	if (!pdev->intr.count)
		return;

	irq_set.start = 0;
	irq_set.count = 0;
	irq_set.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set.index = VFIO_PCI_MSIX_IRQ_INDEX;

	ioctl(pdev->device_fd, VFIO_DEVICE_SET_IRQS, &irq_set);

	for (i = 0; i < pdev->intr.count; i++) {
		if (pdev->intr.efds[i] != -1)
			close(pdev->intr.efds[i]);
	}

	free(pdev->intr.efds);
	pdev->intr.count = 0;
}

void
vfio_pci_device_free(struct vfio_pci_device *pdev)
{
	vfio_pci_disable_interrupts(pdev);
	vfio_pci_device_mem_free(pdev);
	close(pdev->device_fd);
	vfio_clear_group(pdev->group_fd);

	log_write(LOG_DEBUG, "%s: Device freed\n", pdev->name);

	if (!vfio_cfg.active_groups) {
		close(vfio_cfg.container_fd);
		vfio_cfg.container_fd = -1;
		log_write(LOG_DEBUG, "VFIO container closed\n");
	}
}
