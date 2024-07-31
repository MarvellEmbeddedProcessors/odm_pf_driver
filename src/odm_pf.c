/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "odm_pf.h"
#include "pmem.h"
#include "vfio_pci.h"

struct odm_dev *
odm_pf_probe()
{
	struct odm_dev *odm_pf;

	odm_pf = calloc(1, sizeof(*odm_pf));
	if (!odm_pf) {
		log_write(LOG_ERR, "Failed to alloc memory for odm_pf struct\n");
		return NULL;
	}

	strncpy(odm_pf->pdev.name, ODM_PF_PCI_BDF, sizeof(odm_pf->pdev.name));
	if (vfio_pci_device_setup(&odm_pf->pdev)) {
		log_write(LOG_ERR, "Failed to setup vfio pci device\n");
		return NULL;
	}

	odm_pf->pmem = pmem_alloc("/odm_pmem", sizeof(*odm_pf->pmem));
	if (!odm_pf->pmem)
		return NULL;

	log_write(LOG_DEBUG, "%s: Probe successful\n", odm_pf->pdev.name);
	return odm_pf;
}

void
odm_pf_release(struct odm_dev *odm_pf)
{
	if (odm_pf == NULL)
		return;

	if (odm_pf->pmem)
		pmem_free("/odm_pmem");
	if (odm_pf->pdev.device_fd)
		vfio_pci_device_free(&odm_pf->pdev);
	free(odm_pf);
}
