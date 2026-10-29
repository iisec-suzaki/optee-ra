// SPDX-License-Identifier: BSD-2-Clause
/*
 * Minimal i.MX uSDHC driver for OP-TEE core.
 *
 * Scope is deliberately narrow: enough of the controller and of the eMMC
 * protocol to move RPMB frames, so that secure storage does not depend on
 * tee-supplicant (and therefore on Linux user space) being up.
 *
 * Register layout and reset sequence follow U-Boot's fsl_esdhc_imx driver.
 */

#include <drivers/imx_usdhc.h>
#include <imx.h>
#include <imx-regs.h>
#include <io.h>
#include <kernel/cache_helpers.h>
#include <kernel/delay.h>
#include <mm/core_memprot.h>
#include <string.h>
#include <trace.h>
#include <util.h>

register_phys_mem_pgdir(MEM_AREA_IO_SEC, CFG_IMX_USDHC_BASE,
			CORE_MMU_PGDIR_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, IOMUXC_BASE, CORE_MMU_PGDIR_SIZE);

/* Controller registers (offsets from the uSDHC base) */
#define USDHC_DSADDR		0x00
#define USDHC_BLKATTR		0x04
#define USDHC_CMDARG		0x08
#define USDHC_XFERTYP		0x0c
#define USDHC_CMDRSP0		0x10
#define USDHC_CMDRSP1		0x14
#define USDHC_CMDRSP2		0x18
#define USDHC_CMDRSP3		0x1c
#define USDHC_PRSSTAT		0x24
#define USDHC_PROCTL		0x28
#define USDHC_SYSCTL		0x2c
#define USDHC_IRQSTAT		0x30
#define USDHC_IRQSTATEN		0x34
#define USDHC_IRQSIGEN		0x38
/* Holds SMP_CLK_SEL/EXE_TUNE on i.MX8M; set by HS200 tuning, kept by RSTA */
#define USDHC_AUTOCMD12_ERR	0x3c
#define USDHC_WML		0x44
#define USDHC_MIXCTRL		0x48
#define USDHC_DLLCTRL		0x60
#define USDHC_CLKTUNECTRL	0x68
#define USDHC_STROBE_DLL_CTRL	0x70
#define USDHC_TUNING_CTRL	0xcc
#define TUNING_CTRL_STD_EN	BIT32(24)
#define STROBE_DLL_CTRL_RESET	BIT32(1)
#define USDHC_VENDORSPEC	0xc0
#define USDHC_MMCBOOT		0xc4

#define BLKATTR_BLKCNT_SHIFT	16

/* Watermark fields; the rest of the register holds the burst lengths. */
#define WML_RD_MASK		0x000000ff
#define WML_WR_MASK		0x00ff0000

#define XFERTYP_CMDINX_SHIFT	24
#define XFERTYP_DPSEL		BIT32(21)
#define XFERTYP_CICEN		BIT32(20)
#define XFERTYP_CCCEN		BIT32(19)
#define XFERTYP_RSPTYP_NONE	0
#define XFERTYP_RSPTYP_136	SHIFT_U32(1, 16)
#define XFERTYP_RSPTYP_48	SHIFT_U32(2, 16)
#define XFERTYP_RSPTYP_48_BUSY	SHIFT_U32(3, 16)

#define MIXCTRL_MSBSEL		BIT32(5)
#define MIXCTRL_DTDSEL		BIT32(4)
#define MIXCTRL_BCEN		BIT32(1)
#define MIXCTRL_DMAEN		BIT32(0)

#define PRSSTAT_CIHB		BIT32(0)
#define PRSSTAT_CDIHB		BIT32(1)
#define PRSSTAT_DLA		BIT32(2)
/* DAT0 line level: the card pulls it low while it is programming. */
#define PRSSTAT_DAT0_LEVEL	BIT32(24)

#define SYSCTL_INITA		BIT32(27)
#define SYSCTL_RSTA		BIT32(24)
#define SYSCTL_RSTT		BIT32(28)	/* reset tuning */
#define SYSCTL_RST_FIFO		BIT32(22)
#define SYSCTL_RSTC		BIT32(25)
#define SYSCTL_RSTD		BIT32(26)
#define SYSCTL_CLOCK_MASK	0x0000fff0
#define SYSCTL_TIMEOUT_MASK	0x000f0000

#define IRQSTAT_CC		BIT32(0)
#define IRQSTAT_TC		BIT32(1)
#define IRQSTATEN_BWR		BIT32(4)
#define IRQSTATEN_BRR		BIT32(5)
#define IRQSTAT_BRR		BIT32(5)
#define IRQSTAT_CTOE		BIT32(16)
#define IRQSTAT_CCE		BIT32(17)
#define IRQSTAT_CEBE		BIT32(18)
#define IRQSTAT_CIE		BIT32(19)
#define IRQSTAT_DTOE		BIT32(20)
#define IRQSTAT_DCE		BIT32(21)
#define IRQSTAT_DEBE		BIT32(22)
#define IRQSTAT_DMAE		BIT32(28)
/* DMA finished moving the data (DINT) */
#define IRQSTAT_DINT		BIT32(3)
#define IRQSTAT_ERROR		(IRQSTAT_CTOE | IRQSTAT_CCE | IRQSTAT_CEBE | \
				 IRQSTAT_CIE | IRQSTAT_DTOE | IRQSTAT_DCE | \
				 IRQSTAT_DEBE | IRQSTAT_DMAE)

#define VENDORSPEC_CKEN		BIT32(14)
#define VENDORSPEC_PEREN	BIT32(13)
#define VENDORSPEC_HCKEN	BIT32(12)
#define VENDORSPEC_IPGEN	BIT32(11)
#define VENDORSPEC_FRC_SDCLK_ON	BIT32(8)

/* eMMC commands (JEDEC JESD84) */
#define MMC_CMD_GO_IDLE_STATE		0
#define MMC_CMD_SEND_OP_COND		1
#define MMC_CMD_ALL_SEND_CID		2
#define MMC_CMD_SET_RELATIVE_ADDR	3
#define MMC_CMD_SWITCH			6
#define MMC_CMD_SELECT_CARD		7
#define MMC_CMD_SEND_EXT_CSD		8
#define MMC_CMD_SET_BLOCKLEN		16
#define MMC_CMD_READ_MULTIPLE_BLOCK	18
#define MMC_CMD_SET_BLOCK_COUNT		23
#define MMC_CMD_WRITE_MULTIPLE_BLOCK	25

#define MMC_OCR_BUSY			BIT32(31)
#define MMC_OCR_SECTOR_MODE		BIT32(30)
#define MMC_OCR_VOLTAGE_MASK		0x00ff8000

/* EXT_CSD fields we care about */
#define EXT_CSD_PART_CONF		179
#define EXT_CSD_RPMB_MULT		168
#define EXT_CSD_REL_WR_SEC_C		222
#define EXT_CSD_SIZE			512

#define EXT_CSD_PART_ACCESS_MASK	0x7
#define EXT_CSD_PART_ACCESS_RPMB	0x3

#define CMD_TIMEOUT_US			1000000
#define DATA_TIMEOUT_US			15000000
#define OCR_TIMEOUT_US			2000000

struct usdhc_ctx {
	vaddr_t base;
	uint32_t rca;
	uint8_t rpmb_mult;
	uint8_t rel_wr_sec_c;
	uint8_t part_conf;
	/* SYSCTL divider bits this driver programmed, to spot outside changes */
	uint32_t clock_bits;
	uint8_t cid[IMX_USDHC_CID_SIZE];
	uint8_t cur_part;
	bool inited;
};

/*
 * All DMA goes through this driver-owned buffer: page aligned, in core
 * memory the controller (a secure master) can reach, and independent of
 * where callers keep their frames.
 */
#define DMA_BOUNCE_BLOCKS	32
static uint8_t dma_bounce[DMA_BOUNCE_BLOCKS * IMX_USDHC_BLOCK_SIZE]
	__aligned(4096);

static struct usdhc_ctx usdhc_ctx = {
	.rca = 1,
	.cur_part = 0xff,
};

struct mmc_cmd {
	uint16_t idx;
	uint32_t arg;
	uint32_t xfertyp;
	uint32_t resp[4];
	void *data;
	size_t blocks;
	bool write;
};

static vaddr_t usdhc_base(void)
{
	if (!usdhc_ctx.base)
		usdhc_ctx.base = core_mmu_get_va(CFG_IMX_USDHC_BASE,
						 MEM_AREA_IO_SEC, 0x10000);
	return usdhc_ctx.base;
}

/*
 * Pad configuration for uSDHC3, mirroring the pinctrl_usdhc3 group of the
 * i.MX8MP EVK device tree. The core runs before U-Boot proper, which is
 * where these pads would otherwise be muxed, so the driver has to do it.
 *
 * Each entry is { mux register, config register, input select register,
 * mux mode, input value } with the pad settings the device tree applies.
 */
struct pad_cfg {
	uint16_t mux_reg;
	uint16_t conf_reg;
	uint16_t input_reg;
	uint8_t mux_mode;
	uint8_t input_val;
	uint16_t conf_val;
};

static const struct pad_cfg usdhc3_pads[] = {
	{ 0x124, 0x384, 0x604, 0x2, 0x1, 0x190 },	/* CLK */
	{ 0x128, 0x388, 0x60c, 0x2, 0x1, 0x1d0 },	/* CMD */
	{ 0x108, 0x368, 0x610, 0x2, 0x1, 0x1d0 },	/* DATA0 */
	{ 0x10c, 0x36c, 0x614, 0x2, 0x1, 0x1d0 },	/* DATA1 */
	{ 0x110, 0x370, 0x618, 0x2, 0x1, 0x1d0 },	/* DATA2 */
	{ 0x114, 0x374, 0x61c, 0x2, 0x1, 0x1d0 },	/* DATA3 */
	{ 0x11c, 0x37c, 0x620, 0x2, 0x1, 0x1d0 },	/* DATA4 */
	{ 0x0ec, 0x34c, 0x624, 0x2, 0x1, 0x1d0 },	/* DATA5 */
	{ 0x0f0, 0x350, 0x628, 0x2, 0x1, 0x1d0 },	/* DATA6 */
	{ 0x0f4, 0x354, 0x62c, 0x2, 0x1, 0x1d0 },	/* DATA7 */
};

static TEE_Result usdhc_pads_configure(void)
{
	vaddr_t iomux = core_mmu_get_va(IOMUXC_BASE, MEM_AREA_IO_SEC, 0x10000);
	size_t i = 0;

	if (!iomux) {
		EMSG("IOMUXC not mapped");
		return TEE_ERROR_GENERIC;
	}

	for (i = 0; i < ARRAY_SIZE(usdhc3_pads); i++) {
		const struct pad_cfg *p = usdhc3_pads + i;

		io_write32(iomux + p->mux_reg, p->mux_mode);
		io_write32(iomux + p->conf_reg, p->conf_val);
		if (p->input_reg)
			io_write32(iomux + p->input_reg, p->input_val);
	}

	return TEE_SUCCESS;
}

/*
 * The controller's bus side (data buffer and DMA) runs on the shared
 * NAND/uSDHC bus root. Nothing in Linux uses that root once uSDHC3 is
 * disabled in its device tree, so its clock framework switches the root
 * off after boot (clk_disable_unused): commands still answer on the SD
 * clock, but every data phase then stalls. This controller is owned by
 * the TEE, so the TEE keeps its bus clock alive.
 */
#define CCM_NAND_USDHC_BUS_ROOT	0x8900
#define CCM_ROOT_ENABLE		BIT32(28)
/* CCGRn plain (read) register; the SET alias comes from the platform headers */
#define CCM_CCGRn(n)		(0x4000 + (n) * 0x10)

static void usdhc_bus_clock_ensure(vaddr_t ccm, const char *when __maybe_unused)
{
	uint32_t v = io_read32(ccm + CCM_NAND_USDHC_BUS_ROOT);
	uint32_t r = io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET);
	uint32_t g = io_read32(ccm + CCM_CCGRn(CFG_IMX_USDHC_CCM_CCGR));
	bool fix = false;

	if (!(v & CCM_ROOT_ENABLE)) {
		IMSG("uSDHC bus clock root was off (%#"PRIx32") at %s, enabling",
		     v, when);
		io_write32(ccm + CCM_NAND_USDHC_BUS_ROOT, v | CCM_ROOT_ENABLE);
		fix = true;
	}
	if (!(r & CCM_ROOT_ENABLE)) {
		IMSG("uSDHC clock root was off (%#"PRIx32") at %s, enabling",
		     r, when);
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, r | CCM_ROOT_ENABLE);
		fix = true;
	}
	if ((g & 0x3) != 0x3) {
		IMSG("uSDHC clock gate was %#"PRIx32" at %s, opening", g, when);
		io_write32(ccm + CCM_CCGRx_SET(CFG_IMX_USDHC_CCM_CCGR), 0xffffffff);
		fix = true;
	}
	if (fix)
		udelay(10);
}

/*
 * Ungate the controller clock and point its root at the 24 MHz oscillator.
 * Linux is not running yet at this point, but the boot loader may have left
 * the clock gated: touching the registers in that state faults the core.
 */
static TEE_Result usdhc_clock_enable(void)
{
	vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);

	if (!ccm) {
		EMSG("CCM not mapped");
		return TEE_ERROR_GENERIC;
	}

	/* Root clock: enable, source 0 (24 MHz osc), no pre/post divider */
	io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, BIT32(28));
	DMSG("uSDHC CCM root: wrote %#"PRIx32", reads %#"PRIx32, BIT32(28),
	     io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET));

	/* Ungate the peripheral in all power domains */
	io_write32(ccm + CCM_CCGRx_SET(CFG_IMX_USDHC_CCM_CCGR), 0xffffffff);

	usdhc_bus_clock_ensure(ccm, "init");

	return TEE_SUCCESS;
}

static TEE_Result wait_bits_clear(vaddr_t reg, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);

	while (io_read32(reg) & mask) {
		if (timeout_elapsed(tref)) {
			EMSG("bits %#"PRIx32" stuck, register holds %#"PRIx32,
			     mask, io_read32(reg));
			return TEE_ERROR_BUSY;
		}
	}

	return TEE_SUCCESS;
}

/*
 * Controller and clock state on a failure: enough to tell a wedged data
 * phase, a foreign configuration and a gated clock apart.
 */
static void dump_regs(vaddr_t base __maybe_unused)
{
	vaddr_t ccm __maybe_unused = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC,
						      CCM_SIZE);

	EMSG("uSDHC PRSSTAT %#"PRIx32" IRQSTAT %#"PRIx32" MIXCTRL %#"PRIx32
	     " BLKATTR %#"PRIx32" AUTOCMD12_ERR %#"PRIx32,
	     io_read32(base + USDHC_PRSSTAT), io_read32(base + USDHC_IRQSTAT),
	     io_read32(base + USDHC_MIXCTRL), io_read32(base + USDHC_BLKATTR),
	     io_read32(base + USDHC_AUTOCMD12_ERR));
	EMSG("uSDHC PROCTL %#"PRIx32" SYSCTL %#"PRIx32" VENDSPEC %#"PRIx32
	     " DSADDR %#"PRIx32,
	     io_read32(base + USDHC_PROCTL), io_read32(base + USDHC_SYSCTL),
	     io_read32(base + USDHC_VENDORSPEC), io_read32(base + USDHC_DSADDR));
	if (ccm)
		EMSG("CCM usdhc3 root %#"PRIx32" nand_usdhc_bus %#"PRIx32
		     " gate %#"PRIx32,
		     io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET),
		     io_read32(ccm + CCM_NAND_USDHC_BUS_ROOT),
		     io_read32(ccm + CCM_CCGRn(CFG_IMX_USDHC_CCM_CCGR)));
}

static TEE_Result wait_irq(vaddr_t base, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);
	uint32_t stat = 0;

	do {
		stat = io_read32(base + USDHC_IRQSTAT);

		if (stat & IRQSTAT_ERROR) {
			EMSG("uSDHC error, IRQSTAT %#"PRIx32" PRSSTAT %#"PRIx32, stat,
			     io_read32(base + USDHC_PRSSTAT));
			io_write32(base + USDHC_IRQSTAT, stat);
			return TEE_ERROR_COMMUNICATION;
		}

		if ((stat & mask) == mask) {
			io_write32(base + USDHC_IRQSTAT, mask);
			return TEE_SUCCESS;
		}
	} while (!timeout_elapsed(tref));

	EMSG("timeout waiting for IRQSTAT %#"PRIx32", have %#"PRIx32
	     " PRSSTAT %#"PRIx32, mask, stat,
	     io_read32(base + USDHC_PRSSTAT));
	dump_regs(base);

	return TEE_ERROR_BUSY;
}


/*
 * DMA transfer, as U-Boot's fsl_esdhc_imx does it: the data phase is over
 * when both the transfer-complete and the DMA-complete flags are set. On
 * this controller DINT means "DMA finished", not a page-boundary pause, and
 * writing DSADDR while a transfer runs restarts the engine, so the address
 * register is never touched here.
 */
static TEE_Result xfer_data(vaddr_t base, struct mmc_cmd *cmd __unused)
{
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);
	const uint32_t done = IRQSTAT_TC | IRQSTAT_DINT;
	uint32_t stat = 0;

	do {
		stat = io_read32(base + USDHC_IRQSTAT);

		if (stat & IRQSTAT_ERROR) {
			EMSG("data error, IRQSTAT %#"PRIx32, stat);
			io_write32(base + USDHC_IRQSTAT, stat);
			return TEE_ERROR_COMMUNICATION;
		}

		if ((stat & done) == done) {
			io_write32(base + USDHC_IRQSTAT, done);
			return TEE_SUCCESS;
		}
	} while (!timeout_elapsed(tref));

	EMSG("transfer did not complete, IRQSTAT %#"PRIx32" PRSSTAT %#"PRIx32,
	     stat, io_read32(base + USDHC_PRSSTAT));

	return TEE_ERROR_BUSY;
}

/*
 * Clear a wedged transfer. Without this one failure leaves the command and
 * data lines inhibited, so every later command fails as well and the real
 * cause is buried under the fallout.
 */
static void reset_transfer(vaddr_t base)
{
	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RSTC | SYSCTL_RSTD);
	wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RSTC | SYSCTL_RSTD,
			CMD_TIMEOUT_US);
	io_write32(base + USDHC_IRQSTAT, 0xffffffff);
}

static TEE_Result send_cmd(struct mmc_cmd *cmd)
{
	vaddr_t base = usdhc_base();
	uint32_t mixctrl = 0;
	TEE_Result res = TEE_SUCCESS;

	res = wait_bits_clear(base + USDHC_PRSSTAT,
			      PRSSTAT_CIHB | PRSSTAT_CDIHB | PRSSTAT_DLA,
			      CMD_TIMEOUT_US);
	if (res) {
		reset_transfer(base);
		res = wait_bits_clear(base + USDHC_PRSSTAT,
				      PRSSTAT_CIHB | PRSSTAT_CDIHB |
				      PRSSTAT_DLA, CMD_TIMEOUT_US);
	}
	if (res)
		return res;

	io_write32(base + USDHC_IRQSTAT, 0xffffffff);

	if (cmd->data) {
		paddr_t pa = virt_to_phys(dma_bounce);
		size_t len = cmd->blocks * IMX_USDHC_BLOCK_SIZE;

		if (!pa) {
			EMSG("transfer buffer has no physical address");
			return TEE_ERROR_GENERIC;
		}
		if (len > sizeof(dma_bounce)) {
			EMSG("transfer of %zu blocks exceeds the DMA buffer",
			     cmd->blocks);
			return TEE_ERROR_EXCESS_DATA;
		}

		/*
		 * The controller reads and writes memory directly, so the
		 * caches have to be settled around it: clean what is about to
		 * be sent, and drop stale lines over the destination.
		 */
		if (cmd->write) {
			memcpy(dma_bounce, cmd->data, len);
			dcache_clean_range(dma_bounce, len);
		} else {
			dcache_inv_range(dma_bounce, len);
		}

		io_write32(base + USDHC_DSADDR, pa);
		io_write32(base + USDHC_BLKATTR,
			   SHIFT_U32(cmd->blocks, BLKATTR_BLKCNT_SHIFT) |
			   IMX_USDHC_BLOCK_SIZE);
		/*
		 * Watermarks in words: reads are capped at 16 on this
		 * controller, writes take the full 128-word block.
		 *
		 * Only those two fields may be touched. The same register
		 * carries the burst lengths, and writing it whole leaves them
		 * at zero - the controller then accepts data into its buffer
		 * but never puts it on the bus, so a write never completes.
		 */
		io_clrsetbits32(base + USDHC_WML, WML_RD_MASK | WML_WR_MASK,
				SHIFT_U32(0x80, 16) | 0x10);

		/*
		 * Multi-block select follows the command, not the block
		 * count: RPMB moves single frames with the multiple-block
		 * commands, and a write left in single-block mode never
		 * completes - the controller keeps the transfer active while
		 * the device sits idle.
		 */
		mixctrl = MIXCTRL_DMAEN;
		if (cmd->blocks > 1)
			mixctrl |= MIXCTRL_MSBSEL | MIXCTRL_BCEN;
		if (!cmd->write)
			mixctrl |= MIXCTRL_DTDSEL;

		/*
		 * Write the whole register: the upper bits select DDR,
		 * HS400 and tuning modes, none of which apply to legacy
		 * single-data-rate transfers. Preserving them carried
		 * whatever the register held at boot into every transfer.
		 */
		io_write32(base + USDHC_MIXCTRL, mixctrl);
		if (io_read32(base + USDHC_MIXCTRL) != mixctrl)
			EMSG("MIXCTRL wrote %#"PRIx32" reads %#"PRIx32,
			     mixctrl, io_read32(base + USDHC_MIXCTRL));
	}

	io_write32(base + USDHC_CMDARG, cmd->arg);
	io_write32(base + USDHC_XFERTYP,
		   SHIFT_U32(cmd->idx, XFERTYP_CMDINX_SHIFT) | cmd->xfertyp);

	res = wait_irq(base, IRQSTAT_CC, CMD_TIMEOUT_US);
	if (res)
		return res;

	cmd->resp[0] = io_read32(base + USDHC_CMDRSP0);
	cmd->resp[1] = io_read32(base + USDHC_CMDRSP1);
	cmd->resp[2] = io_read32(base + USDHC_CMDRSP2);
	cmd->resp[3] = io_read32(base + USDHC_CMDRSP3);

	if (cmd->data) {
		res = xfer_data(base, cmd);
		if (!res && !cmd->write) {
			size_t len = cmd->blocks * IMX_USDHC_BLOCK_SIZE;

			dcache_inv_range(dma_bounce, len);
			memcpy(cmd->data, dma_bounce, len);
		}
		if (res) {
			EMSG("CMD%"PRIu16" data phase failed, response %#"
			     PRIx32, cmd->idx, cmd->resp[0]);
			dump_regs(base);
			reset_transfer(base);
			/* Start from scratch next time: card and controller */
			usdhc_ctx.inited = false;
			usdhc_ctx.cur_part = 0xff;
		}
		return res;
	}

	return TEE_SUCCESS;
}

static void set_clock(vaddr_t base, uint32_t divisor, uint32_t prescaler)
{
	uint32_t sysctl = 0;
	uint32_t want = 0;
	uint64_t tref = 0;

	/*
	 * Stop only the card clock while the divider changes (U-Boot's
	 * set_sysctl). Gating the module's ipg/hclk enables as well leaves
	 * the divider logic unclocked during the write: the register reads
	 * back but the SD clock stays at the reset divider, /256.
	 */
	io_clrbits32(base + USDHC_VENDORSPEC, VENDORSPEC_CKEN);

	sysctl = io_read32(base + USDHC_SYSCTL);
	sysctl &= ~(SYSCTL_CLOCK_MASK | SYSCTL_TIMEOUT_MASK);
	want = SHIFT_U32(prescaler, 8) | SHIFT_U32(divisor, 4);
	sysctl |= want | SHIFT_U32(14, 16);
	io_write32(base + USDHC_SYSCTL, sysctl);

	/* Wait for the internal clock to settle on the new divider */
	tref = timeout_init_us(CMD_TIMEOUT_US);
	while (!(io_read32(base + USDHC_PRSSTAT) & BIT32(3))) {
		if (timeout_elapsed(tref)) {
			EMSG("uSDHC clock never stabilised, PRSSTAT %#"PRIx32,
			     io_read32(base + USDHC_PRSSTAT));
			break;
		}
	}

	io_setbits32(base + USDHC_VENDORSPEC,
		     VENDORSPEC_CKEN | VENDORSPEC_PEREN | VENDORSPEC_HCKEN |
		     VENDORSPEC_IPGEN | VENDORSPEC_FRC_SDCLK_ON);

	usdhc_ctx.clock_bits = want;
	sysctl = io_read32(base + USDHC_SYSCTL);
	DMSG("uSDHC clock: divider wanted %#"PRIx32", SYSCTL now %#"PRIx32
	     "%s", want, sysctl,
	     ((sysctl & SYSCTL_CLOCK_MASK) == want) ? "" : " (MISMATCH)");
}

static TEE_Result reset_controller(vaddr_t base)
{
	TEE_Result res = TEE_SUCCESS;

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RSTA | SYSCTL_RSTT);
	res = wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RSTA | SYSCTL_RSTT,
			      CMD_TIMEOUT_US);
	if (res)
		return res;

	/* As U-Boot: RSTA leaves these untouched, clear them by hand */
	io_write32(base + USDHC_MMCBOOT, 0);
	io_write32(base + USDHC_MIXCTRL, 0);
	io_write32(base + USDHC_CLKTUNECTRL, 0);
	io_write32(base + USDHC_VENDORSPEC,
		   0x20007809 | (io_read32(base + USDHC_VENDORSPEC) & BIT32(1)));
	io_write32(base + USDHC_DLLCTRL, 0);
	/* HS400 strobe DLL and HS200 standard tuning: off for legacy timing */
	io_write32(base + USDHC_STROBE_DLL_CTRL, STROBE_DLL_CTRL_RESET);
	udelay(10);
	io_write32(base + USDHC_STROBE_DLL_CTRL, 0);
	io_clrbits32(base + USDHC_TUNING_CTRL, TUNING_CTRL_STD_EN);
	/*
	 * As Linux (sdhci-esdhc-imx probe and esdhc_reset_tuning): the
	 * sample-clock select and execute-tuning bits live here on i.MX8M
	 * and survive RSTA. Left set after a HS200 tuning run they make
	 * the receiver sample with the tuned delay clock, which garbles
	 * every response at legacy speed.
	 */
	io_write32(base + USDHC_AUTOCMD12_ERR, 0);
	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RST_FIFO);
	wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RST_FIFO, CMD_TIMEOUT_US);
	/* W1C on BRR clears the IP's execute_tuning_with_clr_buf flag */
	io_write32(base + USDHC_IRQSTAT, IRQSTAT_BRR);
	/* Buffer-ready flags are polled in PRSSTAT, keep them out of IRQSTAT */
	io_write32(base + USDHC_IRQSTATEN,
		   0xffffffff & ~(IRQSTATEN_BRR | IRQSTATEN_BWR));
	io_write32(base + USDHC_IRQSIGEN, 0);
	io_write32(base + USDHC_PROCTL, 0x00000020);

	/* Identification speed: 24 MHz / 64 = 375 kHz */
	set_clock(base, 0, 0x20);

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_INITA);
	return wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_INITA,
			       CMD_TIMEOUT_US);
}

static TEE_Result card_identify(void)
{
	struct mmc_cmd cmd = { };
	uint64_t tref = 0;
	size_t i = 0;
	uint8_t raw[IMX_USDHC_CID_SIZE] = { };
	TEE_Result res = TEE_SUCCESS;

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_GO_IDLE_STATE, .arg = 0,
				.xfertyp = XFERTYP_RSPTYP_NONE };
	res = send_cmd(&cmd);
	if (res) {
		EMSG("CMD0 (go idle) failed, PRSSTAT %#"PRIx32,
		     io_read32(usdhc_base() + USDHC_PRSSTAT));
		return res;
	}
	DMSG("uSDHC: CMD0 ok");

	mdelay(2);

	tref = timeout_init_us(OCR_TIMEOUT_US);
	do {
		cmd = (struct mmc_cmd){ .idx = MMC_CMD_SEND_OP_COND,
					.arg = MMC_OCR_SECTOR_MODE |
					       MMC_OCR_VOLTAGE_MASK,
					.xfertyp = XFERTYP_RSPTYP_48 };
		res = send_cmd(&cmd);
		if (res) {
			EMSG("CMD1 (send op cond) failed, PRSSTAT %#"PRIx32,
			     io_read32(usdhc_base() + USDHC_PRSSTAT));
			return res;
		}

		if (cmd.resp[0] == 0xffffffff) {
			EMSG("CMD1 response reads all ones, PRSSTAT %#"PRIx32,
			     io_read32(usdhc_base() + USDHC_PRSSTAT));
			return TEE_ERROR_COMMUNICATION;
		}
		if (cmd.resp[0] & MMC_OCR_BUSY) {
			DMSG("uSDHC: OCR %#"PRIx32, cmd.resp[0]);
			break;
		}

		if (timeout_elapsed(tref)) {
			EMSG("eMMC stayed busy, last OCR %#"PRIx32,
			     cmd.resp[0]);
			return TEE_ERROR_BUSY;
		}
		mdelay(1);
	} while (true);

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_ALL_SEND_CID, .arg = 0,
				.xfertyp = XFERTYP_RSPTYP_136 | XFERTYP_CCCEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	IMSG("eMMC CID %08"PRIx32"%08"PRIx32"%08"PRIx32"%08"PRIx32,
	     cmd.resp[3], cmd.resp[2], cmd.resp[1], cmd.resp[0]);

	/*
	 * The controller strips the CRC byte, so the response registers
	 * hold CID[127:8]. Rebuild the CID in its JEDEC layout, most
	 * significant byte first with a zero in the CRC position, which is
	 * how the kernel (raw_cid) and tee-supplicant present it. The RPMB
	 * FS derives the authentication key from these bytes, so the layout
	 * must be the same on every path to the device.
	 */
	for (i = 0; i < 4; i++) {
		uint32_t w = cmd.resp[3 - i];

		raw[i * 4] = w >> 24;
		raw[i * 4 + 1] = w >> 16;
		raw[i * 4 + 2] = w >> 8;
		raw[i * 4 + 3] = w;
	}
	if (IS_ENABLED(CFG_IMX_USDHC_CID_COMPAT)) {
		/*
		 * Layout of the first driver revision (CID[127:8] in bytes
		 * 1 to 15). Only for devices whose key was programmed by it.
		 */
		memcpy(usdhc_ctx.cid, raw, sizeof(usdhc_ctx.cid));
	} else {
		memcpy(usdhc_ctx.cid, raw + 1, sizeof(usdhc_ctx.cid) - 1);
		usdhc_ctx.cid[sizeof(usdhc_ctx.cid) - 1] = 0;
	}

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_RELATIVE_ADDR,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	/*
	 * Identification done: run the data phases on the undivided 24 MHz
	 * root clock, which is within the 26 MHz legacy-timing limit of the
	 * device.
	 */
	set_clock(usdhc_base(), 0, 0);

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SELECT_CARD,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48_BUSY |
					   XFERTYP_CCCEN | XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_BLOCKLEN,
				.arg = IMX_USDHC_BLOCK_SIZE,
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };

	return send_cmd(&cmd);
}

static TEE_Result read_ext_csd(uint8_t *ext_csd)
{
	struct mmc_cmd cmd = { .idx = MMC_CMD_SEND_EXT_CSD, .arg = 0,
			       .xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					  XFERTYP_CICEN | XFERTYP_DPSEL,
			       .data = ext_csd, .blocks = 1, .write = false };

	return send_cmd(&cmd);
}

static TEE_Result switch_partition(uint8_t part)
{
	struct mmc_cmd cmd = { };
	uint8_t value = 0;
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.cur_part == part)
		return TEE_SUCCESS;

	/*
	 * Use the partition configuration captured at init rather than
	 * re-reading EXT_CSD here: that read is a data transfer of its own,
	 * and issuing one while the device sits on the RPMB partition only
	 * gives the data line more chances to wedge.
	 */
	value = (usdhc_ctx.part_conf & ~EXT_CSD_PART_ACCESS_MASK) |
		(part & EXT_CSD_PART_ACCESS_MASK);

	/* SWITCH: write byte, index PART_CONF, value, cmd set 0 */
	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SWITCH,
				.arg = SHIFT_U32(3, 24) |
				       SHIFT_U32(EXT_CSD_PART_CONF, 16) |
				       SHIFT_U32(value, 8),
				.xfertyp = XFERTYP_RSPTYP_48_BUSY |
					   XFERTYP_CCCEN | XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	/* Bit 7 of the status reports a rejected SWITCH. */
	if (cmd.resp[0] & BIT32(7)) {
		EMSG("partition switch rejected, card status %#"PRIx32,
		     cmd.resp[0]);
		return TEE_ERROR_GENERIC;
	}

	usdhc_ctx.cur_part = part;

	return TEE_SUCCESS;
}

/* Wait for the card to release DAT0, which it holds low while programming. */
static TEE_Result wait_card_ready(void)
{
	vaddr_t base = usdhc_base();
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);

	do {
		if (io_read32(base + USDHC_PRSSTAT) & PRSSTAT_DAT0_LEVEL)
			return TEE_SUCCESS;
	} while (!timeout_elapsed(tref));

	EMSG("card stayed busy, PRSSTAT %#"PRIx32,
	     io_read32(base + USDHC_PRSSTAT));

	return TEE_ERROR_BUSY;
}


static TEE_Result rpmb_xfer(void *buf, size_t nblocks, bool write)
{
	struct mmc_cmd cmd = { };
	TEE_Result res = TEE_SUCCESS;

	res = imx_usdhc_init();
	if (res)
		return res;

	res = switch_partition(EXT_CSD_PART_ACCESS_RPMB);
	if (res)
		return res;

	res = wait_card_ready();
	if (res)
		return res;

	/*
	 * Bit 31 of SET_BLOCK_COUNT marks a reliable write. The RPMB
	 * specification asks for it on authenticated data writes and key
	 * programming; request frames are plain writes. This driver sets it
	 * on every write: that is the form validated on the board (the device
	 * accepts it for request frames too), and the plain-write form has
	 * not been re-tested since the clock and DMA fixes.
	 */
	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_BLOCK_COUNT,
				.arg = nblocks | (write ? BIT32(31) : 0),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	DMSG("RPMB %s: part %u, block count status %#"PRIx32,
	     write ? "write" : "read", usdhc_ctx.cur_part, cmd.resp[0]);

	cmd = (struct mmc_cmd){ .idx = write ? MMC_CMD_WRITE_MULTIPLE_BLOCK :
					       MMC_CMD_READ_MULTIPLE_BLOCK,
				.arg = 0,
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN | XFERTYP_DPSEL,
				.data = buf, .blocks = nblocks,
				.write = write };

	/*
	 * Stay on the RPMB partition: a request frame and the response that
	 * follows it are one transaction, and switching partitions in between
	 * loses the pending response.
	 */
	return send_cmd(&cmd);
}

/*
 * U-Boot proper (bootstd scanning every MMC device) and Linux both drive
 * this controller too, and leave it in 8-bit HS400 with their own clock.
 * Any of those differs from what this driver programmed, so compare the
 * registers rather than trusting the cached state.
 */
static bool controller_state_foreign(vaddr_t base)
{
	if (io_read32(base + USDHC_PROCTL) & 0x6)		/* DTW != 1-bit */
		return true;
	if ((io_read32(base + USDHC_SYSCTL) & SYSCTL_CLOCK_MASK) !=
	    usdhc_ctx.clock_bits)
		return true;
	if (io_read32(base + USDHC_MIXCTRL) & 0xffffff00)	/* DDR/HS400/tuning */
		return true;
	return false;
}

TEE_Result imx_usdhc_init(void)
{
	uint8_t ext_csd[EXT_CSD_SIZE] = { };
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.inited) {
		vaddr_t base = usdhc_base();
		vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);

		if (ccm)
			usdhc_bus_clock_ensure(ccm, "transfer");

		if (!controller_state_foreign(base))
			return TEE_SUCCESS;

		IMSG("uSDHC reconfigured by another master (PROCTL %#"PRIx32
		     " SYSCTL %#"PRIx32" MIXCTRL %#"PRIx32"), re-initialising",
		     io_read32(base + USDHC_PROCTL),
		     io_read32(base + USDHC_SYSCTL),
		     io_read32(base + USDHC_MIXCTRL));
		dump_regs(base);
		usdhc_ctx.inited = false;
		usdhc_ctx.cur_part = 0xff;
	}

	if (!usdhc_base()) {
		EMSG("uSDHC registers not mapped");
		return TEE_ERROR_GENERIC;
	}

	res = usdhc_pads_configure();
	if (res)
		return res;

	res = usdhc_clock_enable();
	if (res)
		return res;

	res = reset_controller(usdhc_base());
	if (res) {
		EMSG("uSDHC controller reset failed: %#"PRIx32, res);
		return res;
	}

	res = card_identify();
	if (res) {
		EMSG("eMMC identification failed: %#"PRIx32, res);
		return res;
	}

	res = read_ext_csd(ext_csd);
	if (res) {
		EMSG("EXT_CSD read failed: %#"PRIx32, res);
		return res;
	}

	IMSG("eMMC EXT_CSD: PARTITION_SWITCH_TIME %u x10ms, GENERIC_CMD6_TIME %u x10ms,"
	     " REL_WR_SEC_C %u", ext_csd[199], ext_csd[248], ext_csd[222]);
	usdhc_ctx.rpmb_mult = ext_csd[EXT_CSD_RPMB_MULT];
	usdhc_ctx.rel_wr_sec_c = ext_csd[EXT_CSD_REL_WR_SEC_C];
	usdhc_ctx.part_conf = ext_csd[EXT_CSD_PART_CONF];
	usdhc_ctx.cur_part = usdhc_ctx.part_conf & EXT_CSD_PART_ACCESS_MASK;
	usdhc_ctx.inited = true;

	IMSG("uSDHC eMMC ready, RPMB size %u KiB",
	     usdhc_ctx.rpmb_mult * 128);

	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_dev_info(uint8_t *cid, uint8_t *rpmb_size_mult,
			      uint8_t *rel_wr_sec_c)
{
	TEE_Result res = imx_usdhc_init();

	if (res)
		return res;

	if (cid)
		memcpy(cid, usdhc_ctx.cid, sizeof(usdhc_ctx.cid));
	if (rpmb_size_mult)
		*rpmb_size_mult = usdhc_ctx.rpmb_mult;
	if (rel_wr_sec_c)
		*rel_wr_sec_c = usdhc_ctx.rel_wr_sec_c;

	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks)
{
	return rpmb_xfer(buf, nblocks, false);
}

TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks)
{
	TEE_Result res = rpmb_xfer((void *)buf, nblocks, true);

	if (res)
		return res;

	/*
	 * The device programs the data after the transfer completes; the next
	 * command must not arrive while it is still busy.
	 */
	return wait_card_ready();
}

