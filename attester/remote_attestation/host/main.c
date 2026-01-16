#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* For the UUID (found in the TA's h-file(s)) */
#include <remote_attestation_ta.h>

#include "client.h"

void print_binary_in_hex(uint8_t *buf, size_t sz) {
    int i = 0;
    for (i = 0; i < sz; i++)
        fprintf(stdout, "%02x", buf[i]);

    printf("\n");

    return;
}

static int hex_value(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *hex, uint8_t **out, size_t *out_len) {
    size_t n = strlen(hex);
    size_t i = 0, j = 0;
    /* skip 0x prefix if present */
    if (n >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2; n -= 2;
    }
    if (n % 2 != 0) return -1;
    uint8_t *buf = (uint8_t *)malloc(n / 2);
    if (!buf) return -1;
    for (i = 0, j = 0; i < n; i += 2, j++) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i+1]);
        if (hi < 0 || lo < 0) { free(buf); return -1; }
        buf[j] = (uint8_t)((hi << 4) | lo);
    }
    *out = buf;
    *out_len = j;
    return 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --help, -h                Show this help and exit\n");
    printf("  --generate-blackkey       Generate new black key (CAAM only)\n");
    printf("  --convert-key HEX         Convert plain key to black key (CAAM only)\n");
    printf("  --key-hex HEX             Use key blob in hex for signing\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    TEEC_Result res = TEEC_SUCCESS;
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_UUID ta_uuid = TA_REMOTE_ATTESTATION_UUID;
    uint32_t err_origin;

    /* Handle --help before TEE initialization */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Initialize a context connecting us to the TEE */
    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

    res = TEEC_OpenSession(&ctx, &sess, &ta_uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
                           &err_origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x", res,
             err_origin);

    /* Convert plain key to black key */
    if (argc > 1 && strcmp(argv[1], "--convert-key") == 0) {
        uint8_t *plain_key = NULL;
        size_t plain_key_len = 0;

        if (argc > 2) {
            if (parse_hex(argv[2], &plain_key, &plain_key_len) != 0)
                errx(1, "Invalid plain key hex");
        } else {
            errx(1, "Missing key value for conversion");
        }

        if (plain_key_len != 32)
            errx(1, "Plain key must be 32 bytes for P-256");

        TEEC_Operation op_c = {0};
        uint8_t black_key[512] = {0};

        op_c.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                           TEEC_MEMREF_TEMP_OUTPUT,
                                           TEEC_NONE,
                                           TEEC_NONE);
        op_c.params[0].tmpref.buffer = plain_key;
        op_c.params[0].tmpref.size = plain_key_len;
        op_c.params[1].tmpref.buffer = black_key;
        op_c.params[1].tmpref.size = sizeof(black_key);

        printf("\nConverting plain key to black key...\n");
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_CONVERT_TO_BLACKKEY,
                                 &op_c, &err_origin);
        if (res != TEEC_SUCCESS) {
            free(plain_key);
            errx(1, "Convert key failed 0x%x origin 0x%x", res, err_origin);
        }

        printf("BlackKey(hex): ");
        print_binary_in_hex(black_key, op_c.params[1].tmpref.size);
        free(plain_key);
        printf("Key conversion completed.\n");
        return 0;
    }

    /* Generate new black key from scratch */
    if (argc > 1 && strcmp(argv[1], "--generate-blackkey") == 0) {
        TEEC_Operation op_p = {0};
        uint8_t pub_x[32] = {0};
        uint8_t pub_y[32] = {0};
        /* First call: size probe for black key */
        op_p.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                           TEEC_MEMREF_TEMP_OUTPUT,
                                           TEEC_MEMREF_TEMP_OUTPUT,
                                           TEEC_NONE);
        op_p.params[0].tmpref.buffer = NULL;
        op_p.params[0].tmpref.size = 0;
        op_p.params[1].tmpref.buffer = pub_x;
        op_p.params[1].tmpref.size = sizeof(pub_x);
        op_p.params[2].tmpref.buffer = pub_y;
        op_p.params[2].tmpref.size = sizeof(pub_y);
        printf("\nGenerating new black key...\n");
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_GENERATE_BLACKKEY,
                                 &op_p, &err_origin);
        if (res != TEEC_ERROR_SHORT_BUFFER && res != TEEC_SUCCESS)
            errx(1, "Generate black key (size-probe) failed 0x%x origin 0x%x", res, err_origin);
        if (op_p.params[0].tmpref.size == 0)
            errx(1, "Generate black key returned zero-size key blob");
        size_t blob_len = op_p.params[0].tmpref.size;
        uint8_t *blob = (uint8_t *)malloc(blob_len);
        if (!blob) errx(1, "Out of memory");
        /* Second call: fetch key blob */
        op_p.params[0].tmpref.buffer = blob;
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_GENERATE_BLACKKEY,
                                 &op_p, &err_origin);
        if (res != TEEC_SUCCESS) {
            free(blob);
            errx(1, "Generate black key (fetch) failed 0x%x origin 0x%x", res, err_origin);
        }

        printf("BlackKey(hex): ");
        print_binary_in_hex(blob, op_p.params[0].tmpref.size);
        printf("PubX(hex): ");
        print_binary_in_hex(pub_x, sizeof(pub_x));
        printf("PubY(hex): ");
        print_binary_in_hex(pub_y, sizeof(pub_y));
        free(blob);
        printf("Black key generation completed.\n");
        return 0;
    }

    /* Connect to the server and establish a session */
    ChallengeResponseSession *session = open_session();
    if (session == NULL) {
        printf("Failed to open session.\n");
        return 1;
    }

    /* Optional: pass private key from host (SW 'd' or CAAM black key) */
    uint8_t *host_key = NULL;
    size_t host_key_len = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--key-hex") == 0 && i + 1 < argc) {
            if (parse_hex(argv[i+1], &host_key, &host_key_len) != 0)
                errx(1, "Invalid --key-hex value");
            i++;
        }
    }

    /* Request TA to issue evidence based on a given nonce */
    /* The buffer allocated here must be large enough to hold the CBOR evidece
     */
    uint8_t cbor_evidence[1024] = {0};
    TEEC_Operation op = {0};

    /* Setup implementation ID (required by PTA as param[2]) */
    static const uint8_t impl_id[IMPLEMENTATION_ID_LEN] = IMPLEMENTATION_ID;

    if (host_key && host_key_len > 0) {
        /* Params: nonce(in), output(out), impl_id(in), key(in) */
        op.paramTypes = TEEC_PARAM_TYPES(
            TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
            TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INPUT);
    } else {
        /* Params: nonce(in), output(out), impl_id(in), none */
        op.paramTypes = TEEC_PARAM_TYPES(
            TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
            TEEC_MEMREF_TEMP_INPUT, TEEC_NONE);
    }
    op.params[0].tmpref.buffer = (uint8_t *)session->nonce;
    op.params[0].tmpref.size = session->nonce_size;
    op.params[1].tmpref.buffer = cbor_evidence;
    op.params[1].tmpref.size = sizeof(cbor_evidence);
    /* param[2] is implementation_id */
    op.params[2].tmpref.buffer = (void *)impl_id;
    op.params[2].tmpref.size = IMPLEMENTATION_ID_LEN;
    /* param[3] is optional black key */
    if (host_key && host_key_len > 0) {
        op.params[3].tmpref.buffer = host_key;
        op.params[3].tmpref.size = host_key_len;
    }

    printf("\nInvoke TA.\n");
    res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE,
                             &op, &err_origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x", res,
             err_origin);

    printf("Invoked TA successfully.\n\n\n");

    /* Receive CBOR(COSE) evidence from PTA */
    printf("Received evidence of CBOR (COSE) format from PTA.\n\n");

    printf("CBOR(COSE) size: %ld\n", op.params[1].tmpref.size);
    printf("CBOR(COSE): ");
    print_binary_in_hex(op.params[1].tmpref.buffer, op.params[1].tmpref.size);
    printf("\n\n");

    /* Send the generated evidence to the session just established */
    post_evidence(session, op.params[1].tmpref.buffer,
                  op.params[1].tmpref.size);

    if (host_key) free(host_key);

    return 0;
}
