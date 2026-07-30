#include <kernel/linker.h>
#include <kernel/pseudo_ta.h>
#include <pta_remote_attestation.h>

#include "base64.h"
#include "cbor.h"
#include "hash.h"
#include "sign.h"
#include <crypto/crypto.h>
#include <stdlib.h>
#include <string.h>

#ifdef CFG_NXP_CAAM
#include <caam_key.h>
#include <caam_status.h>
#include "ocotp.h"
#endif

#define PTA_NAME "remote_attestation.pta"

#define MAX_KEY_SIZE         4096
#define MAX_NONCE_SIZE       64
#define TEE_SHA256_HASH_SIZE 32

#define EAT_PROFILE     "http://arm.com/psa/2.0.0"
#define MEASURMENT_TYPE "ARoT"
#define MEASUREMENT_TYPE_TEE_OS "PRoT"
#define OS_VERSION_MAX_LEN      32
#ifdef CFG_NXP_CAAM
#define IMPLEMENTATION_ID     "imx8mp-optee-ra-0000000000000001"
#else
#define IMPLEMENTATION_ID     "qemu-optee-ra-000000000000000001"
#endif
#define IMPLEMENTATION_ID_LEN 32
#define SIGNER_ID_LEN   32
#define INSTANCE_ID_LEN 33

#define PUBKEY_COORD_SIZE  32
#define PUBKEY_HEADER_SIZE (PUBKEY_COORD_SIZE + PUBKEY_COORD_SIZE)
#define MIN_KEY_PARAM_SIZE (PUBKEY_HEADER_SIZE + 1)

#ifndef CFG_NXP_CAAM
/* clang-format off */
/* QEMU fallback: test signer-id value (no OCOTP fuses available) */
#define SIGNER_ID_TEST_VALUE                            \
    0xac, 0xbb, 0x11, 0xc7, 0xe4, 0xda, 0x21, 0x72,    \
    0x05, 0x52, 0x3c, 0xe4, 0xce, 0x1a, 0x24, 0x5a,    \
    0xe1, 0xa2, 0x39, 0xae, 0x3c, 0x6b, 0xfd, 0x9e,    \
    0x78, 0x71, 0xf7, 0xe5, 0xd8, 0xba, 0xe8, 0x6b
/* clang-format on */
#define LIFECYCLE_DEFAULT 0x3000
#endif

#ifdef CFG_NXP_CAAM
static TEE_Result caam_to_tee_status(enum caam_status status)
{
    switch (status) {
    case CAAM_NO_ERROR:
        return TEE_SUCCESS;
    case CAAM_OUT_MEMORY:
        return TEE_ERROR_OUT_OF_MEMORY;
    case CAAM_BAD_PARAM:
        return TEE_ERROR_BAD_PARAMETERS;
    case CAAM_SHORT_BUFFER:
        return TEE_ERROR_SHORT_BUFFER;
    default:
        return TEE_ERROR_GENERIC;
    }
}
#endif

/*
 * Derive a PSA client-id (positive int32) from the calling TA's UUID.
 * SHA-256(UUID) lower 31 bits, with bit 31 cleared to guarantee a positive
 * value (positive = SPE caller per PSA spec).
 */
static int32_t derive_client_id(const uint8_t *ta_uuid, size_t uuid_len) {
    uint8_t hash[TEE_SHA256_HASH_SIZE];
    void *ctx = NULL;

    if (!ta_uuid || uuid_len == 0)
        return 1; /* safe fallback */

    if (crypto_hash_alloc_ctx(&ctx, TEE_ALG_SHA256) != TEE_SUCCESS)
        return 1;
    if (crypto_hash_init(ctx) != TEE_SUCCESS ||
        crypto_hash_update(ctx, ta_uuid, uuid_len) != TEE_SUCCESS ||
        crypto_hash_final(ctx, hash, TEE_SHA256_HASH_SIZE) != TEE_SUCCESS) {
        crypto_hash_free_ctx(ctx);
        return 1;
    }
    crypto_hash_free_ctx(ctx);

    /* Take lower 4 bytes of SHA-256 digest, clear sign bit */
    uint32_t raw = ((uint32_t)hash[28] << 24) | ((uint32_t)hash[29] << 16) |
                   ((uint32_t)hash[30] << 8) | hash[31];
    return (int32_t)(raw & 0x7FFFFFFF);
}

static TEE_Result cmd_get_cbor_evidence(uint32_t param_types,
                                        TEE_Param params[TEE_NUM_PARAMS]) {
    const uint8_t *nonce = params[0].memref.buffer;
    const size_t nonce_sz = params[0].memref.size;
    uint8_t *output_buffer = params[1].memref.buffer;
    size_t *output_buffer_len = &params[1].memref.size;
    const uint8_t *ta_uuid = params[2].memref.buffer;
    const size_t ta_uuid_len = params[2].memref.size;
    const uint8_t *serialized_black_key = NULL;
    size_t serialized_black_key_len = 0;
    TEE_Result status = TEE_SUCCESS;

    const char eat_profile[] = EAT_PROFILE;
    const uint8_t *psa_implementation_id =
        (const uint8_t *)IMPLEMENTATION_ID;
    const size_t psa_implementation_id_len = IMPLEMENTATION_ID_LEN;
    const int psa_client_id = derive_client_id(ta_uuid, ta_uuid_len);
    uint8_t signer_id[SIGNER_ID_LEN] = {0};
    int psa_security_lifecycle = 0;
    uint8_t psa_instance_id[INSTANCE_ID_LEN] = {0};
    uint8_t pub_x[PUBKEY_COORD_SIZE] = {0};
    uint8_t pub_y[PUBKEY_COORD_SIZE] = {0};

    uint8_t measurement_value[TEE_SHA256_HASH_SIZE] = {0};
    uint8_t tee_measurement[TEE_SHA256_HASH_SIZE] = {0};
    char os_version[OS_VERSION_MAX_LEN] = {0};
    size_t b64_measurement_value_len = TEE_SHA256_HASH_SIZE * 2;
    char b64_measurement_value[TEE_SHA256_HASH_SIZE * 2] = {0};

    /* Accept output buffer as INOUT or OUTPUT, with/without optional key */
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT)) {
        return TEE_ERROR_BAD_PARAMETERS;
    }

    if (!nonce || !nonce_sz)
        return TEE_ERROR_BAD_PARAMETERS;

    if (!output_buffer || !(*output_buffer_len))
        return TEE_ERROR_BAD_PARAMETERS;

    /* Populate signer-id and lifecycle from platform fuses or fallback */
#ifdef CFG_NXP_CAAM
    status = ocotp_read_srk_hash(signer_id);
    if (status != TEE_SUCCESS)
        return status;

    {
        uint32_t lifecycle_val = 0;
        status = ocotp_get_lifecycle(&lifecycle_val);
        if (status != TEE_SUCCESS)
            return status;
        psa_security_lifecycle = (int)lifecycle_val;
    }
#else
    {
        const uint8_t fallback_signer[SIGNER_ID_LEN] = {SIGNER_ID_TEST_VALUE};
        memcpy(signer_id, fallback_signer, SIGNER_ID_LEN);
        psa_security_lifecycle = LIFECYCLE_DEFAULT;
    }
#endif

    /*
     * param[3] wire format (optional):
     *   PubX(32 bytes) || PubY(32 bytes) || key_blob(N bytes)
     *
     * When provided, the public key coordinates are used to compute the
     * PSA instance-id dynamically:
     *   instance_id = 0x01 || SHA-256(0x04 || PubX || PubY)
     *
     * When absent, the embedded test key's public coordinates are used.
     */
    if (TEE_PARAM_TYPE_GET(param_types, 3) == TEE_PARAM_TYPE_MEMREF_INPUT) {
        const uint8_t *p3 = params[3].memref.buffer;
        size_t p3_len = params[3].memref.size;

        if (p3_len < MIN_KEY_PARAM_SIZE)
            return TEE_ERROR_BAD_PARAMETERS;

        memcpy(pub_x, p3, PUBKEY_COORD_SIZE);
        memcpy(pub_y, p3 + PUBKEY_COORD_SIZE, PUBKEY_COORD_SIZE);
        serialized_black_key = p3 + PUBKEY_HEADER_SIZE;
        serialized_black_key_len = p3_len - PUBKEY_HEADER_SIZE;
    } else {
        /* No external key: use embedded test key coordinates */
        status = get_test_key_pubkey(pub_x, pub_y);
        if (status != TEE_SUCCESS)
            return status;
    }

    /* Compute PSA instance-id from public key */
    status = compute_instance_id(pub_x, pub_y, psa_instance_id);
    if (status != TEE_SUCCESS)
        return status;

    /* Calculate measurement hash of memory */
    status = get_hash_ta_memory(measurement_value, TEE_SHA256_HASH_SIZE);
    if (status != TEE_SUCCESS)
        return status;

    /* For debug print */
    if (base64_encode(measurement_value, TEE_SHA256_HASH_SIZE,
                      b64_measurement_value, &b64_measurement_value_len) != 1) {
        DMSG("Failed to encode measurement_value to base64");
        return TEE_ERROR_GENERIC;
    }
    b64_measurement_value[b64_measurement_value_len] = '\0';
    DMSG("b64_measurement_value: %s", b64_measurement_value);

    /* Measure the OP-TEE OS (core .text + .rodata) for the PRoT component */
    status = get_hash_tee_memory(tee_measurement, TEE_SHA256_HASH_SIZE);
    if (status != TEE_SUCCESS)
        return status;

    /*
     * OP-TEE OS version: first whitespace-delimited token of core_v_str,
     * e.g. "4.6.0" (the build banner is "<ver> (<cc>) #<n> <date> <arch>").
     */
    {
        size_t i = 0;

        while (i < sizeof(os_version) - 1 && core_v_str[i] &&
               core_v_str[i] != ' ') {
            os_version[i] = core_v_str[i];
            i++;
        }
        os_version[i] = '\0';
        if (os_version[0] == '\0')
            memcpy(os_version, "unknown", sizeof("unknown"));
    }
    DMSG("OP-TEE OS version: %s", os_version);

    /*
     * For provisioning: log the PRoT (OP-TEE OS) measurement in base64 so it
     * can be captured once from a device boot and registered as the Veraison
     * reference value (psa.refval-id label "PRoT").
     */
    {
        size_t b64_tee_len = TEE_SHA256_HASH_SIZE * 2;
        char b64_tee[TEE_SHA256_HASH_SIZE * 2] = {0};

        if (base64_encode(tee_measurement, TEE_SHA256_HASH_SIZE, b64_tee,
                          &b64_tee_len) == 1) {
            b64_tee[b64_tee_len] = '\0';
            DMSG("PRoT (OP-TEE OS) measurement-value b64: %s", b64_tee);
        }
    }

    /* Allocate CBOR/COSE work buffers on heap to avoid PTA stack overflow */
    void *heap_cbor = malloc(512);
    void *heap_cose = malloc(512);
    if (!heap_cbor || !heap_cose) {
        free(heap_cbor);
        free(heap_cose);
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    UsefulBuf buffuer_for_cbor = {heap_cbor, 512};
    UsefulBuf buffer_for_cose = {heap_cose, 512};

    /* Build the Software Components: ARoT (this TA) + PRoT (OP-TEE OS) */
    struct psa_sw_component components[] = {
        {
            .measurement_type = MEASURMENT_TYPE,
            .measurement_value = measurement_value,
            .measurement_value_len = TEE_SHA256_HASH_SIZE,
            .version = NULL,
            .signer_id = signer_id,
            .signer_id_len = SIGNER_ID_LEN,
        },
        {
            .measurement_type = MEASUREMENT_TYPE_TEE_OS,
            .measurement_value = tee_measurement,
            .measurement_value_len = TEE_SHA256_HASH_SIZE,
            .version = os_version,
            .signer_id = signer_id,
            .signer_id_len = SIGNER_ID_LEN,
        },
    };

    /* Encode evidence to CBOR */
    UsefulBufC ubc_cbor_evidence = encode_evidence_to_cbor(
        eat_profile, psa_client_id, psa_security_lifecycle,
        psa_implementation_id, psa_implementation_id_len, components,
        sizeof(components) / sizeof(components[0]), psa_instance_id,
        INSTANCE_ID_LEN, nonce, nonce_sz, buffuer_for_cbor);
    if (UsefulBuf_IsNULLC(ubc_cbor_evidence)) {
        DMSG("Failed to encode evidence to CBOR");
        free(heap_cbor);
        free(heap_cose);
        return TEE_ERROR_GENERIC;
    }

    /* Sign the CBOR and generate a COSE evidence */
    UsefulBufC cose_evidence =
        generate_cose(ubc_cbor_evidence, buffer_for_cose,
                      serialized_black_key, serialized_black_key_len);
    if (UsefulBuf_IsNULLC(cose_evidence)) {
        DMSG("Failed to encode CBOR to COSE");
        free(heap_cbor);
        free(heap_cose);
        return TEE_ERROR_GENERIC;
    }

    /* Copy COSE evidence for return buffer */
    memcpy(output_buffer, cose_evidence.ptr, cose_evidence.len);
    *output_buffer_len = cose_evidence.len;

    free(heap_cbor);
    free(heap_cose);
    return TEE_SUCCESS;
}

#ifdef CFG_NXP_CAAM
static TEE_Result cmd_generate_keypair(uint32_t param_types,
                                       TEE_Param params[TEE_NUM_PARAMS]) {
    uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_NONE);
    TEE_Result res = TEE_SUCCESS;
    struct ecc_keypair key = {};
    uint8_t *out_ser = params[0].memref.buffer;
    size_t out_ser_size = params[0].memref.size;
    uint8_t *out_x = params[1].memref.buffer;
    size_t out_x_size = params[1].memref.size;
    uint8_t *out_y = params[2].memref.buffer;
    size_t out_y_size = params[2].memref.size;
    size_t d_len, x_len, y_len;
    const size_t sec_size = 32; /* P-256 */

    if (param_types != exp_pt)
        return TEE_ERROR_BAD_PARAMETERS;

    res = crypto_acipher_alloc_ecc_keypair(&key, TEE_TYPE_ECDSA_KEYPAIR,
                                           sec_size * 8);
    if (res != TEE_SUCCESS)
        return res;
    key.curve = TEE_ECC_CURVE_NIST_P256;

    res = crypto_acipher_gen_ecc_key(&key, sec_size * 8);
    if (res != TEE_SUCCESS)
        goto out_free;

    d_len = crypto_bignum_num_bytes(key.d);
    x_len = crypto_bignum_num_bytes(key.x);
    y_len = crypto_bignum_num_bytes(key.y);

    /* Size probe */
    if (out_ser_size < d_len) {
        params[0].memref.size = d_len;
        res = TEE_ERROR_SHORT_BUFFER;
        goto out_free;
    }
    if (out_x_size < sec_size || out_y_size < sec_size) {
        res = TEE_ERROR_SHORT_BUFFER;
        goto out_free;
    }

    /* Export */
    if (out_ser && d_len) {
        crypto_bignum_bn2bin(key.d, out_ser);
        params[0].memref.size = d_len;
    }
    if (out_x) {
        memset(out_x, 0, sec_size);
        crypto_bignum_bn2bin(key.x, out_x + (sec_size - x_len));
        params[1].memref.size = sec_size;
    }
    if (out_y) {
        memset(out_y, 0, sec_size);
        crypto_bignum_bn2bin(key.y, out_y + (sec_size - y_len));
        params[2].memref.size = sec_size;
    }

out_free:
    crypto_bignum_free(&key.d);
    crypto_bignum_free(&key.x);
    crypto_bignum_free(&key.y);
    return res;
}

static TEE_Result cmd_convert_to_blackkey(uint32_t param_types,
                                          TEE_Param params[TEE_NUM_PARAMS]) {
    uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                      TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_NONE,
                                      TEE_PARAM_TYPE_NONE);
    TEE_Result res = TEE_ERROR_GENERIC;
    enum caam_status caam_res;
    struct caamkey caam_key = {};
    uint8_t *plain_d = params[0].memref.buffer;
    size_t plain_d_size = params[0].memref.size;
    uint8_t *out_ser = params[1].memref.buffer;
    size_t out_ser_size = params[1].memref.size;
    size_t need_size = 0;
    const size_t sec_size = 32; /* P-256 */

    if (param_types != exp_pt)
        return TEE_ERROR_BAD_PARAMETERS;

    if (!plain_d || plain_d_size != sec_size)
        return TEE_ERROR_BAD_PARAMETERS;

    caam_key.key_type = CAAM_KEY_PLAIN_TEXT;
    caam_key.sec_size = plain_d_size;
    caam_key.is_blob = false;

    caam_res = caam_key_alloc(&caam_key);
    if (caam_res != CAAM_NO_ERROR)
        return caam_to_tee_status(caam_res);

    memcpy(caam_key.buf.data, plain_d, plain_d_size);

    caam_res = caam_key_black_encapsulation(&caam_key, CAAM_KEY_BLACK_CCM);
    if (caam_res != CAAM_NO_ERROR) {
        res = caam_to_tee_status(caam_res);
        goto out;
    }

    caam_res = caam_key_serialized_size(&caam_key, &need_size);
    if (caam_res != CAAM_NO_ERROR) {
        res = caam_to_tee_status(caam_res);
        goto out;
    }

    if (out_ser_size < need_size) {
        params[1].memref.size = need_size;
        res = TEE_ERROR_SHORT_BUFFER;
        goto out;
    }

    if (out_ser) {
        caam_res = caam_key_serialize_to_bin(out_ser, out_ser_size, &caam_key);
        if (caam_res != CAAM_NO_ERROR) {
            res = caam_to_tee_status(caam_res);
            goto out;
        }
        params[1].memref.size = need_size;
    }
    res = TEE_SUCCESS;

out:
    caam_key_free(&caam_key);
    return res;
}
#endif /* CFG_NXP_CAAM */

static TEE_Result invoke_command(void *sess_ctx __unused, uint32_t cmd_id,
                                 uint32_t param_types,
                                 TEE_Param params[TEE_NUM_PARAMS]) {
    switch (cmd_id) {
    case PTA_REMOTE_ATTESTATION_GET_CBOR_EVIDENCE:
        return cmd_get_cbor_evidence(param_types, params);
#ifdef CFG_NXP_CAAM
    case PTA_REMOTE_ATTESTATION_GENERATE_KEYPAIR:
        return cmd_generate_keypair(param_types, params);
    case PTA_REMOTE_ATTESTATION_CONVERT_TO_BLACKKEY:
        return cmd_convert_to_blackkey(param_types, params);
#endif
    default:
        break;
    }
    return TEE_ERROR_BAD_PARAMETERS;
}

pseudo_ta_register(.uuid = PTA_REMOTE_ATTESTATION_UUID, .name = PTA_NAME,
                   .flags = PTA_DEFAULT_FLAGS,
                   .invoke_command_entry_point = invoke_command);
