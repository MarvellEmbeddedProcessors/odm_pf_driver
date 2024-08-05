/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"
#include "odm_pf.h"
#include "odm_pf_selftest.h"
#include "pmem.h"
#include "vfio_pci.h"
#include "vfio_pci_irq.h"

static void
test_pmem()
{
	const char *pmem_name = "test_pmem", *msg = "Hello, world!";
	size_t size = 1024;
	pid_t pid;

	pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		/* child process write to pmem and exit without free */
		void *pmem = pmem_alloc(pmem_name, size);

		assert(pmem != NULL);
		strcpy((char *)pmem, msg);
		exit(EXIT_SUCCESS);
	} else {
		/* parent process read from pmem and verify the message */
		wait(NULL);

		void *pmem = pmem_alloc(pmem_name, size);
		assert(pmem != NULL);
		assert(strcmp((char *)pmem, msg) == 0);
		pmem_free(pmem_name);
	}
}

static void
test_odm_register_access(struct odm_dev_config *dev_cfg)
{
	volatile uint64_t *odm_reg;
	struct odm_dev *odm_pf;
	uint64_t val;

	odm_pf = odm_pf_probe(dev_cfg);
	assert(odm_pf != NULL);

#define TEST_REG_VAL 0x12345678
#define TEST_REG_OFF 0x10028
	odm_reg = (volatile uint64_t *)(odm_pf->pdev.mem[0].addr + TEST_REG_OFF);

	val = *odm_reg;

	/* write and read back to verify */
	*odm_reg = TEST_REG_VAL;
	assert(*odm_reg == TEST_REG_VAL);

	*odm_reg = val;
	odm_pf_release(odm_pf);
}

static void
test_odm_irq_handle(void *data)
{
	*(bool *)data = true;
}

static void
test_odm_vfio_pci_irq(struct odm_dev_config *dev_cfg)
{
	struct odm_dev *odm_pf;
	bool interrupt = false;
	uint64_t data = 1;
	int rc;

	odm_pf = odm_pf_probe(dev_cfg);
	assert(odm_pf != NULL);

#define TEST_MSIX_VEC 10
	rc = vfio_pci_msix_enable(&odm_pf->pdev, TEST_MSIX_VEC);
	assert(rc == 0);
	rc = vfio_pci_irq_register(&odm_pf->pdev, TEST_MSIX_VEC, test_odm_irq_handle, &interrupt);
	assert(rc == 0);

	/* fake an interrupt by writing to the eventfd */
	write(odm_pf->pdev.intr.efds[TEST_MSIX_VEC], &data, sizeof(data));
	while (!interrupt)
		sleep(1);

	rc = vfio_pci_irq_unregister(&odm_pf->pdev, TEST_MSIX_VEC);
	assert(rc == 0);

	rc = vfio_pci_msix_disable(&odm_pf->pdev, TEST_MSIX_VEC);
	assert(rc == 0);

	odm_pf_release(odm_pf);
}

void
odm_pf_selftest(struct odm_dev_config *dev_cfg)
{
	test_pmem();
	test_odm_register_access(dev_cfg);
	test_odm_vfio_pci_irq(dev_cfg);

	log_write(LOG_INFO, "ODM PF selftest passed\n");
}
