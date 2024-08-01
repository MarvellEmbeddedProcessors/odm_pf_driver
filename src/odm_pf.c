/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "log.h"
#include "odm_pf.h"
#include "pmem.h"
#include "vfio_pci.h"
#include "vfio_pci_irq.h"

static void
odm_queue_reset(struct odm_dev *odm_pf, uint8_t qid)
{
	int wait_cnt;

	odm_reg_write(odm_pf, ODM_DMAX_QRST(qid), 0x1ULL);
	wait_cnt = 0xFFFFFF;
	while (wait_cnt--) {
		uint64_t regval = odm_reg_read(odm_pf, ODM_DMAX_QRST(qid));

		if (!(regval & 0x1))
			break;
	}
	odm_reg_write(odm_pf, ODM_DMAX_IDS(qid), 0ULL);
}

static void
odm_queue_init(struct odm_dev *odm_pf, uint8_t vf_id, uint8_t qid)
{
	uint8_t hw_qid = vf_id * odm_pf->maxq_per_vf + qid;
	uint64_t reg;

	odm_queue_reset(odm_pf, hw_qid);
	reg = odm_reg_read(odm_pf, ODM_DMAX_IDS(hw_qid));
	reg |= ODM_DMA_IDS_DMA_STRM(vf_id + 1);
	reg |= ODM_DMA_IDS_INST_STRM(vf_id + 1);
	odm_reg_write(odm_pf, ODM_DMAX_IDS(hw_qid), reg);
	odm_pf->setup_done[vf_id] = true;
}

static void
odm_queues_fini(struct odm_dev *odm_pf, uint8_t vf_id)
{
	int maxqs_per_vf = odm_pf->maxq_per_vf;
	int qid, hw_qid_start;

	hw_qid_start = vf_id * maxqs_per_vf;
	for (qid = hw_qid_start; qid < hw_qid_start + maxqs_per_vf; qid++)
		odm_queue_reset(odm_pf, qid);
	odm_pf->setup_done[vf_id] = false;
}

void
odm_pf_update_num_vfs(struct odm_dev *odm_pf)
{
	char sysfs_path[256];
	uint64_t reg_val;
	int num_vfs;
	FILE *file;

	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/bus/pci/devices/%s/sriov_numvfs",
		 ODM_PF_PCI_BDF);
	file = fopen(sysfs_path, "r");
	if (file == NULL) {
		log_write(LOG_ERR, "Could not open the file to read\n");
		return;
	}

	if (fscanf(file, "%x", &num_vfs) != 1) {
		log_write(LOG_ERR, "Could not read the value\n");
		goto close_fd;
	}

	if ((num_vfs > ODM_MAX_VFS) || (num_vfs & (num_vfs - 1))) {
		log_write(LOG_ERR, "Unsupported number of VFs\n");
		goto close_fd;
	}

	if (num_vfs != odm_pf->total_vfs) {
		odm_pf->total_vfs = num_vfs;
		odm_pf->maxq_per_vf = ODM_MAX_QUEUES / num_vfs;
		reg_val = odm_reg_read(odm_pf, ODM_CTL);
		reg_val = (reg_val & ~(0x3 << 4)) | (((__builtin_ffs(num_vfs) - 2) & 0x3) << 4);
		odm_reg_write(odm_pf, ODM_CTL, reg_val);
	}

close_fd:
	fclose(file);
}

void *
odm_vfpf_mbox_thread(void *mbox_work)
{
	struct odm_mbox_work *work = mbox_work;
	struct odm_dev *odm_pf = work->odm_pf;
	uint8_t vf_id, q_idx;

	while (1) {
		pthread_mutex_lock(&work->lock);
		pthread_cond_wait(&work->cond, &work->lock);

		if (work->msg.q.cmd == ODM_MBOX_THREAD_QUIT) {
			pthread_mutex_unlock(&work->lock);
			break;
		}

		vf_id = work->msg.q.vf_id;
		q_idx = work->msg.q.q_idx;
		switch (work->msg.q.cmd) {
			case ODM_DEV_INIT:
				odm_pf_update_num_vfs(odm_pf);
				break;
			case ODM_QUEUE_OPEN:
				odm_queue_init(odm_pf, vf_id, q_idx);
				break;
			case ODM_DEV_CLOSE:
				odm_queues_fini(odm_pf, vf_id);
				break;
			default:
				work->msg.d.err = 0;
		}

		work->msg.d.nvfs = (odm_reg_read(odm_pf, ODM_CTL) >> 4) & 0x3;
		work->msg.d.rsp = work->msg.q.cmd;
		odm_reg_write(odm_pf, ODM_MBOX_PF_VFX_DATAX(vf_id, 0), work->msg.u[0]);
		odm_reg_write(odm_pf, ODM_MBOX_PF_VFX_DATAX(vf_id, 1), work->msg.u[1]);

		pthread_mutex_unlock(&work->lock);
	}
	pthread_exit(NULL);
}

static void
odm_pf_mbox_handler(void *odm_irq)
{
	struct odm_dev *odm_pf = ((struct odm_irq_mem *)odm_irq)->odm_pf;
	struct odm_mbox_work *mbox;
	union odm_mbox_msg_t msg;
	uint64_t reg;
	int i = 0;

	reg = odm_reg_read(odm_pf, ODM_MBOX_VF_PF_INT);

	for (i = 0; i < ODM_MAX_VFS; i++) {
		if (reg & (0x1ULL << i)) {
			msg.u[0] = odm_reg_read(odm_pf, ODM_MBOX_PF_VFX_DATAX(i, 0));
			msg.u[1] = odm_reg_read(odm_pf, ODM_MBOX_PF_VFX_DATAX(i, 1));
			odm_reg_write(odm_pf, ODM_MBOX_VF_PF_INT, (0x1ULL << i));
			msg.q.vf_id = i;

			mbox = &odm_pf->mbox_work[i];
			pthread_mutex_lock(&mbox->lock);
			mbox->msg = msg;
			pthread_cond_signal(&mbox->cond);
			pthread_mutex_unlock(&mbox->lock);
		}
	}
}

static int
odm_setup_mbox(struct odm_dev *odm_pf)
{
	int ret, i = 0;

	/* Disable the mbox interrupts and enable bits */
	odm_reg_write(odm_pf, ODM_MBOX_VF_PF_INT_ENA_W1C, 0xffff);
	odm_reg_write(odm_pf, ODM_MBOX_VF_PF_INT, 0xffff);

	ret = vfio_pci_msix_enable(&odm_pf->pdev, ODM_MBOX_VF_PF_IRQ);
	if (ret) {
		log_write(LOG_ERR, "ODM_PF: MBOX IRQ enable failed\n");
		return -1;
	}

	ret = vfio_pci_irq_register(&odm_pf->pdev, ODM_MBOX_VF_PF_IRQ, odm_pf_mbox_handler,
				    (void *)&odm_pf->irq_mem[ODM_MBOX_VF_PF_IRQ]);
	if (ret) {
		vfio_pci_msix_disable(&odm_pf->pdev, ODM_MBOX_VF_PF_IRQ);
		log_write(LOG_ERR, "ODM_PF: MBOX IRQ register failed\n");
		return -1;
	}

	for (i = 0; i < ODM_MAX_VFS; i++) {
		odm_pf->mbox_work[i].odm_pf = odm_pf;
		pthread_mutex_init(&odm_pf->mbox_work[i].lock, NULL);
		pthread_create(&odm_pf->thread[i], NULL, odm_vfpf_mbox_thread,
			       (void *)&odm_pf->mbox_work[i]);
	}

	/* Enable mbox interrupts */
	odm_reg_write(odm_pf, ODM_MBOX_VF_PF_INT_ENA_W1S, 0xffff);

	return ret;
}

static void
odm_irq_free(struct odm_dev *odm_pf)
{
	int i = 0;

	/* Clear All Enables */
	odm_reg_write(odm_pf, ODM_PF_RAS_ENA_W1C, ODM_PF_RAS_INT);

	for (i = 0; i < ODM_MAX_REQQ_INT; i++) {
		odm_reg_write(odm_pf, ODM_REQQX_INT(i), ODM_REQQ_INT);
		odm_reg_write(odm_pf, ODM_REQQX_INT_ENA_W1C(i), ODM_REQQ_INT);
	}

	for (i = 0; i < odm_pf->num_vecs; i++) {
		vfio_pci_irq_unregister(&odm_pf->pdev, i);
		vfio_pci_msix_disable(&odm_pf->pdev, i);
	}
	free(odm_pf->irq_mem);
	odm_pf->irq_mem = NULL;
	odm_pf->num_vecs = 0;
}

static
void odm_pf_irq_handler(void *odm_irq)
{
	struct odm_irq_mem *irq_mem = (struct odm_irq_mem *)odm_irq;
	uint64_t reg_val;

	if (irq_mem->index < ODM_MAX_REQQ_INT) {
		reg_val = odm_reg_read(irq_mem->odm_pf, ODM_REQQX_INT(irq_mem->index));
		log_write(LOG_ERR, "q_index: %d, REQQX_INT: 0x%016lx\n", irq_mem->index, reg_val);
		odm_reg_write(irq_mem->odm_pf, ODM_REQQX_INT(irq_mem->index), reg_val);
	} else if (irq_mem->index == ODM_PF_RAS_IRQ) {
		reg_val = odm_reg_read(irq_mem->odm_pf, ODM_PF_RAS);
		log_write(LOG_ERR, "RAS_INT: 0x%016lx\n", reg_val);
		odm_reg_write(irq_mem->odm_pf, ODM_PF_RAS, reg_val);
	} else if (irq_mem->index == ODM_NCBO_ERR_IRQ) {
		reg_val = odm_reg_read(irq_mem->odm_pf, ODM_NCBO_ERR_INFO);
		log_write(LOG_ERR, "NCB_ERR_INT: 0x%016lx\n", reg_val);
		odm_reg_write(irq_mem->odm_pf, ODM_NCBO_ERR_INFO, reg_val);
	} else {
		log_write(LOG_ERR, "invalid intr index: 0x%x\n", irq_mem->index);
	}
}

static int
odm_irq_init(struct odm_dev *odm_pf)
{
	uint16_t i, irq = 0;
	int ret;

	odm_pf->num_vecs = odm_pf->pdev.intr.count;

	odm_pf->irq_mem = calloc(odm_pf->num_vecs, sizeof(struct odm_irq_mem));
	if (odm_pf->irq_mem == NULL) {
		odm_pf->num_vecs = 0;
		return -ENOMEM;
	}

	/* Clear all interrupts and interrupt enables*/
	odm_reg_write(odm_pf, ODM_PF_RAS, ODM_PF_RAS_INT);
	odm_reg_write(odm_pf, ODM_PF_RAS_ENA_W1C, ODM_PF_RAS_INT);

	for (i = 0; i < ODM_MAX_REQQ_INT; i++) {
		odm_reg_write(odm_pf, ODM_REQQX_INT(i), ODM_REQQ_INT);
		odm_reg_write(odm_pf, ODM_REQQX_INT_ENA_W1C(i), ODM_REQQ_INT);
	}

	for (irq = 0; irq < odm_pf->num_vecs; irq++) {
		odm_pf->irq_mem[irq].odm_pf = odm_pf;
		odm_pf->irq_mem[irq].index = irq;
		if (irq == ODM_MBOX_VF_PF_IRQ)
			continue;

		ret = vfio_pci_msix_enable(&odm_pf->pdev, irq);
		if (ret) {
			log_write(LOG_ERR, "ODM_PF: IRQ(%d) enable failed\n", irq);
			goto irq_unregister;
		}

		ret = vfio_pci_irq_register(&odm_pf->pdev, irq, odm_pf_irq_handler,
					    (void *)&odm_pf->irq_mem[irq]);
		if (ret) {
			vfio_pci_msix_disable(&odm_pf->pdev, irq);
			log_write(LOG_ERR, "ODM_PF: IRQ(%d) registration failed\n", irq);
			goto irq_unregister;
		}
	}

	/* Enable all interrupts */
	for (i = 0; i < ODM_MAX_REQQ_INT; i++)
		odm_reg_write(odm_pf, ODM_REQQX_INT_ENA_W1S(i), ODM_REQQ_INT);

	odm_reg_write(odm_pf, ODM_PF_RAS_ENA_W1S, ODM_PF_RAS_INT);

	return 0;

irq_unregister:
	for (i = 0; i < irq; i++) {
		if (irq == ODM_MBOX_VF_PF_IRQ)
			continue;
		vfio_pci_irq_unregister(&odm_pf->pdev, i);
		vfio_pci_msix_disable(&odm_pf->pdev, i);
	}
	free(odm_pf->irq_mem);
	odm_pf->irq_mem = NULL;
	odm_pf->num_vecs = 0;

	return -1;
}

static int
odm_init(struct odm_dev *odm_pf)
{
	uint64_t reg = 0ULL;
	int i;

	for (i = 0; i < ODM_MAX_ENGINES; i++) {
		/* For ODM it is recommended for 64KB FIFO for each engine */
		reg = odm_reg_read(odm_pf, ODM_ENGX_BUF(i));
		reg = (reg & ~0x7f) | (ODM_ENG_MAX_FIFO / ODM_MAX_ENGINES);
		odm_reg_write(odm_pf, ODM_ENGX_BUF(i), reg);
		reg = odm_reg_read(odm_pf, ODM_ENGX_BUF(i));
	}

	reg = ODM_DMA_CONTROL_ZBWCSEN;
	reg |= ODM_DMA_CONTROL_DMA_ENB(0x3);
	odm_reg_write(odm_pf, ODM_DMA_CONTROL, reg);
	odm_reg_write(odm_pf, ODM_CTL, ODM_CTL_EN);
	odm_reg_write(odm_pf, ODM_REQQ_GENBUFF_TH_LIMIT, ODM_TH_VAL);

	/* Configure the MOLR to max value of 512 */
	reg = odm_reg_read(odm_pf, ODM_NCB_CFG);
	reg =  (reg & ~0x3ff) | (0x200 & 0x3ff);
	odm_reg_write(odm_pf, ODM_NCB_CFG, reg);

	return 0;
}

static void
odm_fini(struct odm_dev *odm_pf)
{
	uint64_t reg = 0ULL;
	int engine;

	for (engine = 0; engine < ODM_MAX_ENGINES; engine++)
		odm_reg_write(odm_pf, ODM_ENGX_BUF(engine), reg);

	odm_reg_write(odm_pf, ODM_DMA_CONTROL, reg);
	odm_reg_write(odm_pf, ODM_CTL, ~ODM_CTL_EN);
}

struct odm_dev *
odm_pf_probe()
{
	struct odm_dev *odm_pf;
	int err;

	odm_pf = calloc(1, sizeof(*odm_pf));
	if (!odm_pf) {
		log_write(LOG_ERR, "Failed to alloc memory for odm_pf struct\n");
		return NULL;
	}

	strncpy(odm_pf->pdev.name, ODM_PF_PCI_BDF, sizeof(odm_pf->pdev.name));
	if (vfio_pci_device_setup(&odm_pf->pdev)) {
		log_write(LOG_ERR, "Failed to setup vfio pci device\n");
		goto free_pf;
	}

	odm_pf->pmem = pmem_alloc("/odm_pmem", sizeof(*odm_pf->pmem));
	if (!odm_pf->pmem)
		goto free_vfio;

	log_write(LOG_DEBUG, "%s: Probe successful\n", odm_pf->pdev.name);

	/* Initialize global PF registers */
	err = odm_init(odm_pf);
	if (err) {
		log_write(LOG_ERR, "Failed to initialize odm\n");
		goto free_pmem;
	}

	/* Register interrupts */
	err = odm_irq_init(odm_pf);
	if (err) {
		log_write(LOG_ERR, "ODM: Failed to initialize irq vectors\n");
		goto fini_odm;
	}

	/* Setup mbox */
	err = odm_setup_mbox(odm_pf);
	if (err) {
		log_write(LOG_ERR, "ODM: Failed to setup mbox\n");
		goto free_irq;
	}

	return odm_pf;

free_irq:
	odm_irq_free(odm_pf);
fini_odm:
	odm_fini(odm_pf);
free_pmem:
	pmem_free("/odm_pmem");
free_vfio:
	vfio_pci_device_free(&odm_pf->pdev);
free_pf:
	free(odm_pf);

	return NULL;
}

void
odm_pf_release(struct odm_dev *odm_pf)
{
	int i;

	if (odm_pf == NULL)
		return;

	for (i = 0; i < ODM_MAX_VFS; i++) {
		struct odm_mbox_work *mbox = &odm_pf->mbox_work[i];

		pthread_mutex_lock(&mbox->lock);
		mbox->msg.q.cmd = ODM_MBOX_THREAD_QUIT;
		pthread_cond_signal(&mbox->cond);
		pthread_mutex_unlock(&mbox->lock);
	}

	for (i = 0; i < ODM_MAX_VFS; i++) {
		if (pthread_join(odm_pf->thread[i], NULL) != 0)
			log_write(LOG_ERR, "mbox thread close failed for vf: %d\n", i);
	}

	odm_irq_free(odm_pf);
	odm_fini(odm_pf);
	if (odm_pf->pmem)
		pmem_free("/odm_pmem");
	if (odm_pf->pdev.device_fd)
		vfio_pci_device_free(&odm_pf->pdev);
	free(odm_pf);
}
