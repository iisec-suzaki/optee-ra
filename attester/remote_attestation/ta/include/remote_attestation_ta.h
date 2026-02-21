#ifndef TA_REMOTE_ATTESTATION_H
#define TA_REMOTE_ATTESTATION_H

/*
 * This UUID is generated with uuidgen
 * the ITU-T UUID generator at http://www.itu.int/ITU-T/asn1/uuid.html
 */
#define TA_REMOTE_ATTESTATION_UUID                                             \
    {                                                                          \
        0xa6b53b34, 0x855f, 0x11ee, {                                          \
            0xb9, 0xd1, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02                     \
        }                                                                      \
    }

/* The function ID(s) implemented in this TA */
#define TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE 0
#define TA_REMOTE_ATTESTATION_CMD_GENERATE_BLACKKEY 1
#define TA_REMOTE_ATTESTATION_CMD_CONVERT_TO_BLACKKEY 2

/*
 * Wire format for param[2] (optional key material, Host→TA):
 *   PubX(32 bytes) || PubY(32 bytes) || key_blob(N bytes)
 *
 * When param[2] is provided, its size must be >= MIN_KEY_PARAM_SIZE.
 * When param[2] is NONE, the PTA uses the embedded test key.
 */
#define PUBKEY_COORD_SIZE    32
#define PUBKEY_HEADER_SIZE   (PUBKEY_COORD_SIZE + PUBKEY_COORD_SIZE)
#define MIN_KEY_PARAM_SIZE   (PUBKEY_HEADER_SIZE + 1)

#if defined(HOST_BUILD)
typedef TEEC_UUID UUID_TYPE;
#else
typedef TEE_UUID UUID_TYPE;
#endif

#endif /*TA_REMOTE_ATTESTATION_H*/
