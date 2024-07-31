/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"
#include "pmem.h"

#define PMEM_NAME_LEN 64

struct pmem_info {
	char name[PMEM_NAME_LEN];
	void *addr;
	size_t size;
	struct pmem_info *next;
};

struct pmem_info *pmem_list;

static int
pmem_list_update(const char *name, void *addr, size_t size)
{
	struct pmem_info *new_info;

	new_info = malloc(sizeof(struct pmem_info));
	if (new_info == NULL)
		return -1;

	strncpy(new_info->name, name, PMEM_NAME_LEN);
	new_info->addr = addr;
	new_info->size = size;
	new_info->next = pmem_list;

	pmem_list = new_info;

	return 0;
}

void *
pmem_alloc(const char *name, size_t size)
{
	void *addr = NULL;
	int pmem_fd = 0;

	pmem_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
	if (pmem_fd == -1) {
		log_write(LOG_ERR, "Failed to open shared memory\n");
		goto exit;
	}

	if (ftruncate(pmem_fd, size) == -1) {
		log_write(LOG_ERR, "Failed to truncate shared memory file\n");
		goto exit;
	}

	addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, pmem_fd, 0);
	if (addr == MAP_FAILED) {
		log_write(LOG_ERR, "Failed to mmap shared memory file\n");
		goto exit;
	}

	if (pmem_list_update(name, addr, size)) {
		log_write(LOG_ERR, "Failed to update pmem_list\n");
		goto exit;
	}

	log_write(LOG_DEBUG, "Allocated shared memory %s\n", name);

	return addr;

exit:
	if (pmem_fd)
		close(pmem_fd);

	if (addr)
		munmap(addr, size);

	return NULL;
}

static struct pmem_info *
pmem_list_get(const char *name)
{
	struct pmem_info *info = pmem_list;

	while (info) {
		if (strncmp(info->name, name, PMEM_NAME_LEN) == 0)
			return info;

		info = info->next;
	}

	return NULL;
}

static void
pmem_list_remove(struct pmem_info *info)
{
	struct pmem_info *prev = NULL;
	struct pmem_info *curr = pmem_list;

	while (curr) {
		if (curr == info) {
			if (prev)
				prev->next = curr->next;
			else
				pmem_list = curr->next;

			free(curr);
			break;
		}

		prev = curr;
		curr = curr->next;
	}
}

int
pmem_free(const char *name)
{
	struct pmem_info *info;

	info = pmem_list_get(name);
	if (info == NULL) {
		log_write(LOG_ERR, "Failed to get pmem_info\n");
		return -1;
	}

	if (munmap(info->addr, info->size) == -1) {
		log_write(LOG_ERR, "Failed to unmap shared memory address\n");
		return -1;
	}

	if (shm_unlink(name) == -1) {
		log_write(LOG_ERR, "Failed to unlink shared memory file\n");
		return -1;
	}

	pmem_list_remove(info);

	log_write(LOG_DEBUG, "Freed shared memory %s\n", name);
	return 0;
}
