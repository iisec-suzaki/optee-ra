#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <inttypes.h>
#include <string.h>

#include <pta_attestation.h>
#include <pta_remote_attestation.h>
#include <remote_attestation_ta.h>

#ifdef CFG_REMOTE_ATTESTATION_PERF
/*
 * Performance measurement logging (CFG_REMOTE_ATTESTATION_PERF=y).
 * User TAs have no access to a high-resolution counter, so TA-level
 * durations come from TEE_GetSystemTime and are accurate to ~1 ms only.
 * Reported in microseconds for consistency with the PTA/host log lines:
 *   RA_PERF|ta|<event>|<duration_us>|key=<embedded|plain|black>
 */
static uint64_t ra_perf_now_us(void) {
    TEE_Time t = {};

    TEE_GetSystemTime(&t);
    return ((uint64_t)t.seconds * 1000 + t.millis) * 1000;
}

/* packed_key_len is the params[2] size: PubX(32) || PubY(32) || blob(N) */
static const char *ra_perf_keymode(size_t packed_key_len) {
    if (packed_key_len == 0)
        return "embedded";
    return packed_key_len > 96 ? "black" : "plain";
}
#endif

TEE_Result call_pta_for_cbor_evidence(uint32_t param_types,
                                      TEE_Param params[4]) {
    TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
    TEE_Result res = TEE_ERROR_GENERIC;
    uint32_t ret_orig = 0;
    uint8_t *nonce_buf = NULL;
    uint8_t *out_buf = NULL;
    uint8_t *key_buf = NULL;
    size_t nonce_len = 0;
    size_t out_len = 0;

#ifdef CFG_REMOTE_ATTESTATION_PERF
    uint64_t perf_cmd_start = ra_perf_now_us();
    uint64_t perf_invoke_start = 0;
    uint64_t perf_invoke_us = 0;
    const char *perf_keymode = "embedded";

    if (TEE_PARAM_TYPE_GET(param_types, 2) == TEE_PARAM_TYPE_MEMREF_INPUT)
        perf_keymode = ra_perf_keymode(params[2].memref.size);
#endif

    res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &sess,
                            &ret_orig);
    if (res != TEE_SUCCESS) {
        EMSG("TEE_OpenTASession failed\n");
        goto cleanup_return;
    }

    /*
     * Host → TA param layout:
     *   [0] nonce (INPUT)
     *   [1] output (INOUT or OUTPUT)
     *   [2] packed key (INPUT, optional)
     *   [3] unused
     */
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE)) {
        res = TEE_ERROR_BAD_PARAMETERS;
        goto cleanup_return;
    }

    if (!params[0].memref.buffer || !params[0].memref.size ||
        !params[1].memref.buffer || !params[1].memref.size) {
        res = TEE_ERROR_BAD_PARAMETERS;
        goto cleanup_return;
    }

    nonce_len = params[0].memref.size;
    out_len = params[1].memref.size;

    nonce_buf = TEE_Malloc(nonce_len, 0);
    if (!nonce_buf) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto cleanup_return;
    }
    TEE_MemMove(nonce_buf, params[0].memref.buffer, nonce_len);

    out_buf = TEE_Malloc(out_len, 0);
    if (!out_buf) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto cleanup_return;
    }

    /* Send our TA UUID to PTA for client-id derivation */
    TEE_UUID ta_uuid = TA_REMOTE_ATTESTATION_UUID;

    /* Optional key blob from host (packed key now in params[2]) */
    void *key_blob = NULL;
    size_t key_blob_len = 0;
    if (TEE_PARAM_TYPE_GET(param_types, 2) == TEE_PARAM_TYPE_MEMREF_INPUT) {
        key_blob = params[2].memref.buffer;
        key_blob_len = params[2].memref.size;
        if (key_blob && key_blob_len > 0) {
            key_buf = TEE_Malloc(key_blob_len, 0);
            if (!key_buf) {
                res = TEE_ERROR_OUT_OF_MEMORY;
                goto cleanup_return;
            }
            TEE_MemMove(key_buf, key_blob, key_blob_len);
            key_blob = key_buf;
        }
    }

    /* Forward params to PTA */
    uint32_t pta_param_types;
    TEE_Param pta_params[4] = {{.memref.buffer = nonce_buf,
                                .memref.size = nonce_len},
                               {.memref.buffer = out_buf,
                                .memref.size = out_len},
                               {.memref.buffer = &ta_uuid,
                                .memref.size = sizeof(ta_uuid)},
                               {.memref.buffer = NULL, .memref.size = 0}};

    if (key_blob && key_blob_len > 0) {
        pta_params[3].memref.buffer = key_blob;
        pta_params[3].memref.size = key_blob_len;
        pta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_INOUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT);
    } else {
        pta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_INOUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_NONE);
    }

#ifdef CFG_REMOTE_ATTESTATION_PERF
    perf_invoke_start = ra_perf_now_us();
#endif
    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_REMOTE_ATTESTATION_GET_CBOR_EVIDENCE,
                              pta_param_types, pta_params, &ret_orig);
#ifdef CFG_REMOTE_ATTESTATION_PERF
    perf_invoke_us = ra_perf_now_us() - perf_invoke_start;
#endif
    if (res != TEE_SUCCESS) {
        EMSG("TEE_InvokeTACommand failed\n");
        goto cleanup_return;
    }
    /* Update buffer size actually used　*/
    if (pta_params[1].memref.size > out_len) {
        res = TEE_ERROR_SHORT_BUFFER;
        goto cleanup_return;
    }
    TEE_MemMove(params[1].memref.buffer, out_buf, pta_params[1].memref.size);
    params[1].memref.size = pta_params[1].memref.size;

cleanup_return:
    if (key_buf)
        TEE_Free(key_buf);
    if (out_buf)
        TEE_Free(out_buf);
    if (nonce_buf)
        TEE_Free(nonce_buf);
    TEE_CloseTASession(sess);
#ifdef CFG_REMOTE_ATTESTATION_PERF
    /* Durations measured before printing; ~1 ms resolution (see above) */
    if (perf_invoke_us) {
        uint64_t perf_cmd_us = ra_perf_now_us() - perf_cmd_start;

        IMSG("RA_PERF|ta|pta_invoke|%" PRIu64 "|key=%s", perf_invoke_us,
             perf_keymode);
        IMSG("RA_PERF|ta|cmd_total|%" PRIu64 "|key=%s", perf_cmd_us,
             perf_keymode);
    }
#endif
    return res;
}

/*******************************************************************************
 * Mandatory TA functions.
 ******************************************************************************/
TEE_Result TA_CreateEntryPoint(void) {
    /* Nothing to do */
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
    /* Nothing to do */
    return;
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param __unused params[4],
                                    void __unused **sess_ctx) {
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE)) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *sess_ctx) {
    /* Nothing to do */
    return;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    switch (cmd_id) {
    case TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE:
        return call_pta_for_cbor_evidence(param_types, params);

    case TA_REMOTE_ATTESTATION_CMD_GENERATE_BLACKKEY: {
        /* Generate new black key from scratch (CAAM only) */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess = TEE_HANDLE_NULL;
        TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
        uint32_t ret_orig = 0;
        TEE_Result res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess, &ret_orig);
        if (res != TEE_SUCCESS)
            return res;
        res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                                   PTA_REMOTE_ATTESTATION_GENERATE_KEYPAIR,
                                   param_types, params, &ret_orig);
        TEE_CloseTASession(sess);
        return res;
    }

    case TA_REMOTE_ATTESTATION_CMD_CONVERT_TO_BLACKKEY: {
        /* Convert plain key to black key (CAAM only) */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_NONE,
                                           TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess = TEE_HANDLE_NULL;
        TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
        uint32_t ret_orig = 0;
        TEE_Result res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess, &ret_orig);
        if (res != TEE_SUCCESS)
            return res;
        res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                                   PTA_REMOTE_ATTESTATION_CONVERT_TO_BLACKKEY,
                                   param_types, params, &ret_orig);
        TEE_CloseTASession(sess);
        return res;
    }

    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}
