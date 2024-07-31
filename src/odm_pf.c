/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "odm_pf.h"
#include "pmem.h"
#include "vfio_pci.h"

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
		goto vfio_fail;
	}

	odm_pf->pmem = pmem_alloc("/odm_pmem", sizeof(*odm_pf->pmem));
	if (!odm_pf->pmem)
		goto pmem_fail;

	log_write(LOG_DEBUG, "%s: Probe successful\n", odm_pf->pdev.name);

	/* Initialize global PF registers */
	err = odm_init(odm_pf);
	if (err) {
		log_write(LOG_ERR, "Failed to initialize odm\n");
		goto init_fail;
	}

	return odm_pf;

init_fail:
	pmem_free("/odm_pmem");
pmem_fail:
	vfio_pci_device_free(&odm_pf->pdev);
vfio_fail:
	free(odm_pf);

	return NULL;
}

void
odm_pf_release(struct odm_dev *odm_pf)
{
	if (odm_pf == NULL)
		return;

	odm_fini(odm_pf);
	if (odm_pf->pmem)
		pmem_free("/odm_pmem");
	if (odm_pf->pdev.device_fd)
		vfio_pci_device_free(&odm_pf->pdev);
	free(odm_pf);
}
