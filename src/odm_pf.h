/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#ifndef __ODM_PF_H__
#define __ODM_PF_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "errno.h"
#include "log.h"
#include "vfio_pci.h"
#include "uuid.h"

#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

#define ODM_PF_PCI_BDF "0000:08:00.0"

/* PCI BAR nos */
#define PCI_ODM_PF_CFG_BAR		0
#define PCI_ODM_PF_MSIX_BAR		4
#define PCI_ODM_VF_CFG_BAR		0
#define PCI_ODM_VF_MSIX_BAR		4

/* MSI-X interrupts */
#define ODM_MAX_REQQ_INT		32

#define ODM_MAX_ENGINES			2
#define ODM_MAX_VFS			16
#define ODM_MAX_QUEUES			32

/* FIFO in terms of KB */
#define ODM_ENG_MAX_FIFO		128

/****************  Macros for register modification ************/
#define ODM_DMA_IDS_INST_STRM(x)		((uint64_t)((x) & 0xff) << 40)
#define ODM_DMA_IDS_GET_INST_STRM(x)		(((x) >> 40) & 0xff)

#define ODM_DMA_IDS_DMA_STRM(x)			((uint64_t)((x) & 0xff) << 32)
#define ODM_DMA_IDS_GET_DMA_STRM(x)		(((x) >> 32) & 0xff)

#define ODM_ENG_BUF_BASE(x)			(((x) & 0x3fULL) << 16)
#define ODM_ENG_BUF_GET_BASE(x)			(((x) >> 16) & 0x3fULL)

#define ODM_DMA_ENG_EN_QEN(x)			((x) & 0xffULL)
#define ODM_DMA_ENG_EN_GET_QEN(x)		((x) & 0xffULL)

#define ODM_DMA_ENG_EN_MOLR(x)			(((x) & 0x3ffULL) << 32)
#define ODM_DMA_ENG_EN_GET_MOLR(x)		(((x) >> 32) & 0x3ffULL)

#define ODM_DMA_CONTROL_DMA_ENB(x)		(((x) & 0x3fULL) << 48)
#define ODM_DMA_CONTROL_GET_DMA_ENB(x)		(((x) >> 48) & 0x3fULL)

#define ODM_DMA_CONTROL_LDWB			(0x1ULL << 32)
#define ODM_DMA_CONTROL_ZBWCSEN			(0x1ULL << 39)
#define ODM_DMA_CONTROL_UIO_DIS			(0x1ULL << 55)

#define ODM_CTL_EN				(0x1ULL)

/******************** Macros for interrupts ************************/
#define ODM_REQQ_INT_INSTRFLT			BIT_ULL(0)
#define ODM_REQQ_INT_RDFLT			BIT_ULL(1)
#define ODM_REQQ_INT_WRFLT			BIT_ULL(2)
#define ODM_REQQ_INT_CSFLT			BIT_ULL(3)
#define ODM_REQQ_INT_INST_DBO			BIT_ULL(4)
#define ODM_REQQ_INT_INST_FILL_INVAL		BIT_ULL(6)
#define ODM_REQQ_INT_INSTR_PSN			BIT_ULL(7)
#define ODM_REQQ_INT_INSTR_TIMEOUT		BIT_ULL(9)

#define ODM_REQQ_INT \
	(ODM_REQQ_INT_INSTRFLT		| \
	ODM_REQQ_INT_RDFLT		| \
	ODM_REQQ_INT_WRFLT		| \
	ODM_REQQ_INT_CSFLT		| \
	ODM_REQQ_INT_INST_DBO		| \
	ODM_REQQ_INT_INST_FILL_INVAL	| \
	ODM_REQQ_INT_INSTR_PSN		| \
	ODM_REQQ_INT_INSTR_TIMEOUT)

#define ODM_PF_RAS_EBI_DAT_PSN		BIT_ULL(0)
#define ODM_PF_RAS_NCB_DAT_PSN		BIT_ULL(1)
#define ODM_PF_RAS_NCB_CMD_PSN		BIT_ULL(2)
#define ODM_PF_RAS_INT \
	(ODM_PF_RAS_EBI_DAT_PSN  | \
	 ODM_PF_RAS_NCB_DAT_PSN  | \
	 ODM_PF_RAS_NCB_CMD_PSN)

/***************** Registers ******************/
#define ODM_DMAX_IDS(x)				(0x18ULL | ((x) << 11))
#define ODM_DMAX_QRST(x)			(0x30ULL | ((x) << 11))
#define ODM_CSCLK_ACTIVE_PC			(0x10000ULL)
#define ODM_CTL					(0x10010ULL)
#define ODM_DMA_CONTROL				(0x10018ULL)
#define ODM_DMA_INTL_SEL			(0x10028ULL)
#define ODM_DMA_ENGX_EN(x)			(0x10040ULL | ((x) << 3))
#define ODM_NCB_CFG				(0x100A0ULL)
#define ODM_ENGX_BUF(x)				(0x100C0ULL | ((x) << 3))
#define ODM_PF_RAS				(0x10308ULL)
#define ODM_PF_RAS_W1S				(0x10310ULL)
#define ODM_PF_RAS_ENA_W1C			(0x10318ULL)
#define ODM_PF_RAS_ENA_W1S			(0x10320ULL)
#define ODM_REQQX_INT(x)			(0x12C00ULL | ((x) << 5))
#define ODM_REQQX_INT_W1S(x)			(0x13000ULL | ((x) << 5))
#define ODM_REQQX_INT_ENA_W1C(x)		(0x13800ULL | ((x) << 5))
#define ODM_REQQX_INT_ENA_W1S(x)		(0x13C00ULL | ((x) << 5))
#define ODM_MBOX_PF_VFX_DATAX(v, d)		(0x16000ULL | ((v) << 4) | ((d) << 3))
#define ODM_MBOX_VF_PF_INT			(0x16300ULL)
#define ODM_MBOX_VF_PF_INT_W1S			(0x16308ULL)
#define ODM_MBOX_VF_PF_INT_ENA_W1C		(0x16310ULL)
#define ODM_MBOX_VF_PF_INT_ENA_W1S		(0x16318ULL)
#define ODM_REQQ_GENBUFF_TH_LIMIT		(0x17000ULL)
#define ODM_NCBO_ERR_INFO			(0x17200ULL)
#define ODM_NCBO_ERR_INT			(0x17300ULL)

#define ODM_TH_VAL				(0x108030A020C01040ULL)

#define ODM_PF_RAS_IRQ				(0x20)
#define ODM_MBOX_VF_PF_IRQ			(0x21)
#define ODM_NCBO_ERR_IRQ			(0x22)

#define ODM_DEV_INIT		0x1
#define ODM_DEV_CLOSE		0x2
#define ODM_QUEUE_OPEN		0x3
#define ODM_QUEUE_CLOSE		0x4
#define ODM_REG_DUMP		0x5
#define ODM_MBOX_THREAD_QUIT	0x6

struct odm_mbox_dev_msg_t {
	/* Response code */
	uint64_t rsp : 8;
	/* Number of VFs */
	uint64_t nvfs : 2;
	uint64_t err : 6;
	/* Reserved */
	uint64_t rsvd : 48;
};

struct odm_mbox_queue_msg_t {
	/* Command code */
	uint64_t cmd : 8;
	/* VF ID to configure */
	uint64_t vf_id : 8;
	/* Queue index in VF */
	uint64_t q_idx : 8;
	/* Reserved */
	uint64_t rsvd : 40;
};

union odm_mbox_msg_t {
	uint64_t u[2];
	struct {
		struct odm_mbox_dev_msg_t d;
		struct odm_mbox_queue_msg_t q;
	};
};

struct odm_mbox_work {
	struct odm_dev *odm_pf;
	union odm_mbox_msg_t msg;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

struct odm_irq_mem {
	struct odm_dev *odm_pf;
	uint16_t index;
};

enum odm_state {
	ODM_DEV_STATE_INIT,
	ODM_DEV_STATE_INIT_DONE,
	ODM_DEV_STATE_RUNNING
};

struct pmem_data {
	enum odm_state dev_state;
	int maxq_per_vf;
	int vfs_in_use;
	bool setup_done[ODM_MAX_VFS];
};

struct odm_dev_config {
	uint32_t eng_sel;
	uint8_t uuid_gbl[UUID_LEN];
	uint8_t num_vfs;
};

struct odm_dev {
	struct vfio_pci_device pdev;
	struct pmem_data *pmem;
	int num_vecs;
	struct odm_irq_mem *irq_mem;
	pthread_t thread[ODM_MAX_VFS];
	struct odm_mbox_work mbox_work[ODM_MAX_VFS];
};

/* ODM PF functions */
struct odm_dev *odm_pf_probe(struct odm_dev_config *dev_cfg);
void odm_pf_release(struct odm_dev *odm_pf);

static inline void
odm_reg_write(struct odm_dev *odm_pf, uint64_t offset, uint64_t val)
{
	if (offset > odm_pf->pdev.mem[0].len) {
		log_write(LOG_ERR, "reg offset is out of range\n");
		return;
	}

	*((volatile uint64_t *)(odm_pf->pdev.mem[0].addr + offset)) = val;
}

static inline uint64_t
odm_reg_read(struct odm_dev *odm_pf, uint64_t offset)
{
	if (offset > odm_pf->pdev.mem[0].len) {
		log_write(LOG_ERR, "reg offset is out of range\n");
		return -ENOMEM;
	}

	return *(volatile uint64_t *)(odm_pf->pdev.mem[0].addr + offset);
}
#endif /* __ODM_PF_H__ */
