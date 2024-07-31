/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#ifndef __ODM_PF_H__
#define __ODM_PF_H__

#include "vfio_pci.h"

#define ODM_PF_PCI_BDF "0000:08:00.0"

struct pmem_data {
	uint64_t rsvd;
};

struct odm_dev {
	struct vfio_pci_device pdev;
	struct pmem_data *pmem;
};

/* ODM PF functions */
struct odm_dev *odm_pf_probe(void);
void odm_pf_release(struct odm_dev *odm_pf);

#endif /* __ODM_PF_H__ */
