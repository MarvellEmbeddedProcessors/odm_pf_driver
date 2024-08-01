/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "odm_pf.h"
#include "pmem.h"
#include "vfio_pci.h"
#include "vfio_pci_irq.h"

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

	/* Register interrupts */
	err = odm_irq_init(odm_pf);
	if (err) {
		log_write(LOG_ERR, "ODM: Failed to initialize irq vectors\n");
		goto irq_fail;
	}

	return odm_pf;
irq_fail:
	odm_fini(odm_pf);
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

	odm_irq_free(odm_pf);
	odm_fini(odm_pf);
	if (odm_pf->pmem)
		pmem_free("/odm_pmem");
	if (odm_pf->pdev.device_fd)
		vfio_pci_device_free(&odm_pf->pdev);
	free(odm_pf);
}
