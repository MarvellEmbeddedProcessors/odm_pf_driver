/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

/**
 * @file
 *
 * VFIO PCI interrupt library.
 *
 * The interrupt library is used to register and unregister specific interrupt
 * vectors. The register requires a callback function to be provided. The
 * interrupt callback function will be called with the argument provided in
 * cb_arg when an interrupt is received. The callback function should be short
 * and non-blocking as it will be called in the interrupt handler. The interrupt
 * library is thread-safe.
 *
 * Enabling an interrupt is a two step process. First, the interrupt should be
 * enabled using the vfio_pci_msix_enable() function. Then, the interrupt should
 * be registered using the vfio_pci_irq_register() function. Disabling an
 * interrupt is also a two step process. First, the interrupt should be
 * unregistered using the vfio_pci_irq_unregister() function. Then, the
 * interrupt should be disabled using the vfio_pci_msix_disable() function.
 *
 * Example code-flow:
 * 1. Enable the interrupt using vfio_pci_msix_enable().
 * 2. Register the interrupt using vfio_pci_irq_register().
 * 3. Wait for the interrupt.
 * 4. The callback function will be called when the interrupt is received.
 * 5. Unregister the interrupt using vfio_pci_irq_unregister().
 * 6. Disable the interrupt using vfio_pci_msix_disable().
 */

#ifndef _VFIO_PCI_IRQ_H_
#define _VFIO_PCI_IRQ_H_

#include "vfio_pci.h"

/* Callback function type for interrupt */
typedef void (*vfio_pci_irq_cb_t)(void *cb_arg);

/**
 * Register a specific interrupt vector. The interrupt should be enabled using the
 * vfio_pci_msix_enable() function before it is registered.
 *
 * @param	pdev		The PCI device to register the interrupt.
 * @param	vec		The interrupt vector.
 * @param	callback	The callback function to be called when the interrupt is received.
 * @param	cb_arg		The argument to be passed to the callback function.
 * @return			0 on success, -1 on failure.
 */
int vfio_pci_irq_register(struct vfio_pci_device *pdev, uint16_t vec, vfio_pci_irq_cb_t callback,
			  void *cb_arg);

/**
 * Unregister a specific interrupt vector. The interrupt can be disabled using the
 * vfio_pci_msix_disable() function after it is unregistered.
 *
 * @param	pdev		The PCI device to unregister the interrupt.
 * @param	vec		The interrupt vector.
 * @return			0 on success, -1 on failure.
 */
int vfio_pci_irq_unregister(struct vfio_pci_device *pdev, uint16_t vec);

#endif /* _VFIO_PCI_IRQ_H_ */
