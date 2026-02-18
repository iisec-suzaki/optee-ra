#ifndef __PTA_REMOTE_ATTESTATION_H
#define __PTA_REMOTE_ATTESTATION_H

#define PTA_REMOTE_ATTESTATION_UUID                                            \
    {                                                                          \
        0xa77955f9, 0xeea1, 0x44fd, {                                          \
            0xad, 0xd5, 0x4a, 0x9d, 0x96, 0x2a, 0xfc, 0xf5                     \
        }                                                                      \
    }

/*
 * Return a CBOR(COSE) evidence
 *
 * [in]     memref[0]        Nonce
 * [out]    memref[1]        Output buffer
 * [in]     memref[2]        Caller TA UUID (16 bytes)
 * [in]     memref[3]        (optional) Serialized black key for signing
 *
 * Return codes:
 * TEE_SUCCESS
 * TEE_ERROR_ACCESS_DENIED  - Caller is not a user space TA
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 * TEE_ERROR_SHORT_BUFFER   - Output buffer size less than required
 */
#define PTA_REMOTE_ATTESTATION_GET_CBOR_EVIDENCE 0x0

/*
 * Generate ECC P-256 keypair (CAAM black key)
 *
 * [out]    memref[0]        Serialized black key (size-probe allowed)
 * [out]    memref[1]        Public X (32 bytes)
 * [out]    memref[2]        Public Y (32 bytes)
 */
#define PTA_REMOTE_ATTESTATION_GENERATE_KEYPAIR 0x1

/*
 * Convert plain ECC private key to CAAM black key
 *
 * [in]     memref[0]        Plain private key d (32 bytes for P-256)
 * [out]    memref[1]        Serialized black key (size-probe allowed)
 */
#define PTA_REMOTE_ATTESTATION_CONVERT_TO_BLACKKEY 0x2

#endif /* __PTA_REMOTE_ATTESTATION_H */
