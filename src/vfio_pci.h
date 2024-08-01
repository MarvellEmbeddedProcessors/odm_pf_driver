/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

/**
 * @file
 *
 * VFIO PCI library
 *
 * This file contains VFIO PCI library APIs. The library provides
 * APIs to probe VFIO PCI devices and map the resources. The library
 * also provides APIs to enable and disable MSI-X interrupts.
 */

#ifndef __VFIO_PCI_H__
#define __VFIO_PCI_H__

#include <pthread.h>
#include <stdint.h>

#include "uuid.h"

struct vfio_pci_mem_resouce {
	uint32_t index; /**< Resource index. */
	uint8_t *addr;  /**< Mapped virtual address. */
	uint64_t len;   /**< Length of the resource. */
};

struct vfio_intr_data {
	uint32_t count;       /**< Number of MSI-X vectors. */
	int32_t *efds;        /**< Eventfd file descriptors. */
	pthread_mutex_t lock; /**< Lock for interrupt conf */
};

/** VFIO PCI device */
struct vfio_pci_device {
	char name[32];                    /**< PCI BDF */
	uint8_t uuid[UUID_LEN];
	int device_fd;                    /**< VFIO device fd */
	int group_fd;                     /**< VFIO group fd */
	unsigned int num_resource;        /**< Number of device resources */
	struct vfio_pci_mem_resouce *mem; /**< Device resources */
	struct vfio_intr_data intr;       /**< Interrupt data */
};

/* End of structure vfio_pci_device. */

/**
 * Probe a VFIO pci device and map its regions. Upon a successful probe,
 * the device details are set in the memory referenced by the pdev pointer.
 *
 * @param	pdev	Pointer to VFIO pci device structure.
 * @return		Zero on success.
 */
int vfio_pci_device_setup(struct vfio_pci_device *pdev);

/**
 * Release a VFIO pci device and free the associated memory.
 *
 * @param	pdev	Pointer to VFIO pci device structure.
 */
void vfio_pci_device_free(struct vfio_pci_device *pdev);

/**
 * Enable MSI-X interrupts for a VFIO pci device.
 *
 * @param	pdev	Pointer to VFIO pci device structure.
 * @param	vector	MSI-X vector to enable.
 * @return		Zero on success.
 */
int vfio_pci_msix_enable(struct vfio_pci_device *pdev, uint32_t vector);

/**
 * Disable MSI-X interrupts for a VFIO pci device.
 *
 * @param	pdev	Pointer to VFIO pci device structure.
 * @param	vector	MSI-X vector to disable.
 * @return		Zero on success.
 */
int vfio_pci_msix_disable(struct vfio_pci_device *pdev, uint32_t vector);

#endif /* __VFIO_PCI_H__ */
