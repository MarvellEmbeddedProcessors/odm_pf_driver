# SPDX-License-Identifier: Marvell-MIT
# Copyright(C) 2024 Marvell.

executable('odm_pf_driver',
	   'log.c', 'main.c', 'odm_pf.c', 'odm_pf_selftest.c', 'pmem.c', 'vfio_pci.c',
	   'vfio_pci_irq.c', 'uuid.c',
	   dependencies: [librt, libpthread],
           install : true,
)
