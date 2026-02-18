// SPDX-License-Identifier: BSD-2-Clause
/*
 * Read SRK hash and security configuration from i.MX8MP OCOTP fuses
 * for PSA Attestation Token signer-id and lifecycle claims.
 *
 * Uses the OP-TEE imx_ocotp driver which properly handles CCM clock
 * gating — direct io_read32() on OCOTP registers hangs when the
 * OCOTP clock is gated by Linux.
 */

#include "ocotp.h"

#include <drivers/imx_ocotp.h>
#include <string.h>
#include <trace.h>

static bool srk_is_zero(const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (buf[i])
            return false;
    }
    return true;
}

/*
 * Read SRK hash (8 x 32-bit fuse words = 256 bits) from OCOTP.
 * Banks 6-7, words 0-3 each, stored as big-endian byte array.
 */
TEE_Result ocotp_read_srk_hash(uint8_t signer_id[OCOTP_SRK_SIZE])
{
    uint32_t bank, word;
    size_t idx = 0;

    for (bank = OCOTP_SRK_BANK_START; bank <= OCOTP_SRK_BANK_END; bank++) {
        for (word = 0; word < 4; word++) {
            uint32_t val = 0;
            TEE_Result res = imx_ocotp_read(bank, word, &val);

            if (res != TEE_SUCCESS) {
                EMSG("imx_ocotp_read(bank=%u, word=%u) failed: 0x%x",
                     bank, word, res);
                return res;
            }
            /* Store as big-endian */
            signer_id[idx++] = (uint8_t)(val >> 24);
            signer_id[idx++] = (uint8_t)(val >> 16);
            signer_id[idx++] = (uint8_t)(val >> 8);
            signer_id[idx++] = (uint8_t)(val);
        }
    }

    DMSG("OCOTP SRK hash:");
    DHEXDUMP(signer_id, OCOTP_SRK_SIZE);

    return TEE_SUCCESS;
}

/*
 * Determine PSA lifecycle state from OCOTP fuses:
 *
 *   SRK fuses   SEC_CONFIG   Lifecycle
 *   all-zero    Open         0x1000 (ASSEMBLY_AND_TEST)
 *   all-zero    Closed       0x1000 (ASSEMBLY_AND_TEST, abnormal)
 *   written     Open         0x2000 (ROT_PROVISIONING)
 *   written     Closed       0x3000 (SECURED)
 */
TEE_Result ocotp_get_lifecycle(uint32_t *lifecycle)
{
    uint8_t srk[OCOTP_SRK_SIZE] = {0};
    uint32_t sec_cfg = 0;
    bool srk_empty;
    bool closed;
    TEE_Result res;

    if (!lifecycle)
        return TEE_ERROR_GENERIC;

    res = ocotp_read_srk_hash(srk);
    if (res != TEE_SUCCESS)
        return res;

    srk_empty = srk_is_zero(srk, sizeof(srk));

    res = imx_ocotp_read(OCOTP_SEC_CONFIG_BANK, OCOTP_SEC_CONFIG_WORD,
                         &sec_cfg);
    if (res != TEE_SUCCESS) {
        EMSG("imx_ocotp_read(SEC_CONFIG) failed: 0x%x", res);
        return res;
    }
    closed = (sec_cfg & OCOTP_SEC_CONFIG_BIT) != 0;

    DMSG("OCOTP SEC_CONFIG=0x%08" PRIx32 " closed=%d srk_empty=%d",
         sec_cfg, closed, srk_empty);

    if (srk_empty) {
        *lifecycle = PSA_LIFECYCLE_ASSEMBLY_AND_TEST;
    } else if (!closed) {
        *lifecycle = PSA_LIFECYCLE_ROT_PROVISIONING;
    } else {
        *lifecycle = PSA_LIFECYCLE_SECURED;
    }

    return TEE_SUCCESS;
}
