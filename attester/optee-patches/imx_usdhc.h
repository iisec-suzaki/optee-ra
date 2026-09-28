/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Minimal i.MX uSDHC driver for OP-TEE core: eMMC RPMB access without
 * relying on tee-supplicant.
 *
 * Only what RPMB needs: controller bring-up, eMMC identification and
 * 512-byte block transfers on the RPMB partition.
 */
#ifndef __DRIVERS_IMX_USDHC_H
#define __DRIVERS_IMX_USDHC_H

#include <stdbool.h>
#include <stdint.h>
#include <tee_api_types.h>

#define IMX_USDHC_BLOCK_SIZE	512U

/*
 * Bring up the controller and the attached eMMC device.
 * Idempotent: later calls return TEE_SUCCESS without touching the device.
 */
TEE_Result imx_usdhc_init(void);

/*
 * Identification of the attached device, in the form the RPMB layer needs:
 * the raw CID, the RPMB size multiplier (EXT_CSD 168) and the reliable
 * write sector count (EXT_CSD 222). Any output pointer may be NULL.
 */
#define IMX_USDHC_CID_SIZE	16

TEE_Result imx_usdhc_dev_info(uint8_t *cid, uint8_t *rpmb_size_mult,
			      uint8_t *rel_wr_sec_c);

/*
 * Read or write @nblocks of IMX_USDHC_BLOCK_SIZE bytes on the RPMB
 * partition. The caller passes complete RPMB data frames; authentication
 * is done by the RPMB layer, not here.
 */
TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks);

/*
 * Every write is issued as a reliable write (see rpmb_xfer()). The device is
 * left on the RPMB partition afterwards: nothing else uses the eMMC.
 */
TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks);

#endif /* __DRIVERS_IMX_USDHC_H */
