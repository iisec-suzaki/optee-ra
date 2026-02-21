/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef PTA_REMOTE_ATTESTATION_OCOTP_H
#define PTA_REMOTE_ATTESTATION_OCOTP_H

#include <tee_api_types.h>
#include <stdint.h>

#define OCOTP_SRK_BANK_START 6
#define OCOTP_SRK_BANK_END   7
#define OCOTP_SRK_SIZE       32

#define OCOTP_SEC_CONFIG_BANK 1
#define OCOTP_SEC_CONFIG_WORD 3
#define OCOTP_SEC_CONFIG_BIT  (1U << 25)

#define PSA_LIFECYCLE_ASSEMBLY_AND_TEST 0x1000
#define PSA_LIFECYCLE_ROT_PROVISIONING  0x2000
#define PSA_LIFECYCLE_SECURED           0x3000

TEE_Result ocotp_read_srk_hash(uint8_t signer_id[OCOTP_SRK_SIZE]);
TEE_Result ocotp_get_lifecycle(uint32_t *lifecycle);

#endif /* PTA_REMOTE_ATTESTATION_OCOTP_H */
