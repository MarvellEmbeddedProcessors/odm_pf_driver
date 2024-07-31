/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

/**
 * @file
 *
 * Persistent memory library
 *
 * APIs to allocate and free shared memory.
 */

#ifndef __PMEM_H__
#define __PMEM_H__

#include <stddef.h>

/**
 * Get shared memory.
 *
 * @param	name	Name of the shared memory.
 * @param	size	Size of the shared memory.
 * @return		Pointer to the shared memory.
 */
void *pmem_alloc(const char *name, size_t size);

/**
 * Free shared memory.
 *
 * @param	name	Name of the shared memory.
 * @return		0 on success, -1 on failure.
 */
int pmem_free(const char *name);

#endif /* __PMEM_H__ */
