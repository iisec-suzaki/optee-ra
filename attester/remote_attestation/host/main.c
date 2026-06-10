#include <err.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* For the UUID (found in the TA's h-file(s)) */
#include <remote_attestation_ta.h>

#include "client.h"
#include "perf.h"

/* Performance measurement logging (see perf.h) */
int ra_perf_enabled = 0;
const char *ra_perf_keymode = "-";

uint64_t ra_perf_now_us(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
}

void ra_perf_log(const char *event, uint64_t duration_us) {
    if (ra_perf_enabled)
        printf("RA_PERF|host|%s|%" PRIu64 "|key=%s\n", event, duration_us,
               ra_perf_keymode);
}

static int get_random_nonce(uint8_t *nonce, size_t sz) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t n = 0;

    if (!f)
        return -1;
    n = fread(nonce, 1, sz, f);
    fclose(f);
    return n == sz ? 0 : -1;
}

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
    printf("  --pubx-hex HEX            Public key X coordinate (32 bytes hex)\n");
    printf("  --puby-hex HEX            Public key Y coordinate (32 bytes hex)\n");
    printf("  --perf                    Print RA_PERF timing logs (or RA_PERF=1)\n");
    printf("  --loop N                  Repeat the attestation N times\n");
    printf("  --no-server               Skip the verifier; use a local random nonce\n");
    printf("\n");
    printf("When using --key-hex with a CAAM black key, also supply --pubx-hex\n");
    printf("and --puby-hex so the PTA can compute the correct PSA instance-id.\n");
    printf("Alternatively, pass the FullKey (PubX||PubY||blob) directly to --key-hex.\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    TEEC_Result res = TEEC_SUCCESS;
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_UUID ta_uuid = TA_REMOTE_ATTESTATION_UUID;
    uint32_t err_origin;

    /* Parse arguments: error on unknown or invalid usage */
    bool has_key = false;
    bool has_pubx = false;
    bool has_puby = false;
    uint8_t *host_key = NULL;
    size_t host_key_len = 0;
    uint8_t *host_pub_x = NULL;
    size_t host_pub_x_len = 0;
    uint8_t *host_pub_y = NULL;
    size_t host_pub_y_len = 0;
    bool mode_generate = false;
    bool mode_convert = false;
    bool no_server = false;
    long loop_count = 1;

    {
        const char *env_perf = getenv("RA_PERF");

        if (env_perf && strcmp(env_perf, "0") != 0)
            ra_perf_enabled = 1;
    }

    if (argc > 1 && strcmp(argv[1], "--generate-blackkey") == 0) {
        if (argc == 3 && strcmp(argv[2], "--perf") == 0)
            ra_perf_enabled = 1;
        else if (argc != 2)
            errx(1, "--generate-blackkey takes no arguments (except --perf)");
        mode_generate = true;
    } else if (argc > 1 && strcmp(argv[1], "--convert-key") == 0) {
        if (argc == 4 && strcmp(argv[3], "--perf") == 0)
            ra_perf_enabled = 1;
        else if (argc != 3)
            errx(1, "--convert-key requires a hex key argument");
        mode_convert = true;
    }

    if (!mode_generate && !mode_convert) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_usage(argv[0]);
                return 0;
            } else if (strcmp(argv[i], "--key-hex") == 0) {
            if (has_key)
                errx(1, "--key-hex specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --key-hex");
            if (parse_hex(argv[i + 1], &host_key, &host_key_len) != 0)
                errx(1, "Invalid --key-hex value");
            has_key = true;
            i++;
        } else if (strcmp(argv[i], "--pubx-hex") == 0) {
            if (has_pubx)
                errx(1, "--pubx-hex specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --pubx-hex");
            if (parse_hex(argv[i + 1], &host_pub_x, &host_pub_x_len) != 0)
                errx(1, "Invalid --pubx-hex value");
            if (host_pub_x_len != 32)
                errx(1, "--pubx-hex must be exactly 32 bytes");
            has_pubx = true;
            i++;
        } else if (strcmp(argv[i], "--puby-hex") == 0) {
            if (has_puby)
                errx(1, "--puby-hex specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --puby-hex");
            if (parse_hex(argv[i + 1], &host_pub_y, &host_pub_y_len) != 0)
                errx(1, "Invalid --puby-hex value");
            if (host_pub_y_len != 32)
                errx(1, "--puby-hex must be exactly 32 bytes");
            has_puby = true;
            i++;
            } else if (strcmp(argv[i], "--perf") == 0) {
                ra_perf_enabled = 1;
            } else if (strcmp(argv[i], "--loop") == 0) {
                if (i + 1 >= argc)
                    errx(1, "Missing value for --loop");
                loop_count = strtol(argv[i + 1], NULL, 10);
                if (loop_count < 1)
                    errx(1, "Invalid --loop value");
                i++;
            } else if (strcmp(argv[i], "--no-server") == 0) {
                no_server = true;
            } else {
                errx(1, "Unknown argument: %s", argv[i]);
            }
        }

        if ((has_pubx || has_puby) && !has_key)
            errx(1, "--pubx-hex/--puby-hex require --key-hex");
        if (has_pubx != has_puby)
            errx(1, "--pubx-hex and --puby-hex must be specified together");
    }

    uint64_t t0 = 0;

    /* Initialize a context connecting us to the TEE */
    t0 = ra_perf_now_us();
    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed with code 0x%x", res);
    ra_perf_log("teec_init", ra_perf_now_us() - t0);

    t0 = ra_perf_now_us();
    res = TEEC_OpenSession(&ctx, &sess, &ta_uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
                           &err_origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x", res,
             err_origin);
    ra_perf_log("teec_open_session", ra_perf_now_us() - t0);

    /* Convert plain key to black key */
    if (mode_convert) {
        uint8_t *plain_key = NULL;
        size_t plain_key_len = 0;

        if (parse_hex(argv[2], &plain_key, &plain_key_len) != 0)
            errx(1, "Invalid plain key hex");

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
        t0 = ra_perf_now_us();
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_CONVERT_TO_BLACKKEY,
                                 &op_c, &err_origin);
        if (res != TEEC_SUCCESS) {
            free(plain_key);
            errx(1, "Convert key failed 0x%x origin 0x%x", res, err_origin);
        }
        ra_perf_log("blackkey_convert", ra_perf_now_us() - t0);

        printf("BlackKey(hex): ");
        print_binary_in_hex(black_key, op_c.params[1].tmpref.size);
        free(plain_key);
        printf("Key conversion completed.\n");
        return 0;
    }

    /* Generate new black key from scratch */
    if (mode_generate) {
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
        t0 = ra_perf_now_us();
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
        /* Includes both invocations (size probe + fetch = two keygens) */
        ra_perf_log("blackkey_generate", ra_perf_now_us() - t0);

        printf("BlackKey(hex): ");
        print_binary_in_hex(blob, op_p.params[0].tmpref.size);
        printf("PubX(hex): ");
        print_binary_in_hex(pub_x, sizeof(pub_x));
        printf("PubY(hex): ");
        print_binary_in_hex(pub_y, sizeof(pub_y));

        /* FullKey = PubX || PubY || blob — pass directly to --key-hex */
        size_t full_len = sizeof(pub_x) + sizeof(pub_y) +
                          op_p.params[0].tmpref.size;
        uint8_t *full = (uint8_t *)malloc(full_len);
        if (full) {
            memcpy(full, pub_x, sizeof(pub_x));
            memcpy(full + sizeof(pub_x), pub_y, sizeof(pub_y));
            memcpy(full + sizeof(pub_x) + sizeof(pub_y), blob,
                   op_p.params[0].tmpref.size);
            printf("FullKey(hex): ");
            print_binary_in_hex(full, full_len);
            free(full);
        }

        free(blob);
        printf("Black key generation completed.\n");
        return 0;
    }

    /* Optional: pass private key from host (SW 'd' or CAAM black key) */

    /*
     * Build packed key param: PubX(32) || PubY(32) || key_blob(N)
     *
     * If --key-hex value is >= 65 bytes, treat it as a FullKey
     * (PubX||PubY||blob already concatenated).
     * Otherwise, --pubx-hex and --puby-hex are required alongside --key-hex.
     */
    uint8_t *packed_key_param = NULL;
    size_t packed_key_param_len = 0;

    if (host_key && host_key_len > 0) {
        if (host_key_len >= MIN_KEY_PARAM_SIZE && !host_pub_x && !host_pub_y) {
            /* FullKey format: already PubX||PubY||blob */
            packed_key_param = host_key;
            packed_key_param_len = host_key_len;
        } else if (host_pub_x && host_pub_y) {
            /* Separate pubkey + key blob */
            packed_key_param_len = 32 + 32 + host_key_len;
            packed_key_param = (uint8_t *)malloc(packed_key_param_len);
            if (!packed_key_param)
                errx(1, "Out of memory");
            memcpy(packed_key_param, host_pub_x, 32);
            memcpy(packed_key_param + 32, host_pub_y, 32);
            memcpy(packed_key_param + 64, host_key, host_key_len);
        } else {
            errx(1, "--key-hex requires --pubx-hex and --puby-hex "
                 "(or pass FullKey directly)");
        }
    }

    /* Key-path label for the RA_PERF log lines */
    if (packed_key_param && packed_key_param_len > 0)
        ra_perf_keymode = packed_key_param_len > 96 ? "black" : "plain";
    else
        ra_perf_keymode = "embedded";

    for (long iter = 0; iter < loop_count; iter++) {
        ChallengeResponseSession *session = NULL;
        uint8_t local_nonce[32] = {0};
        const uint8_t *nonce = NULL;
        size_t nonce_sz = 0;
        uint64_t t_total = ra_perf_now_us();

        if (no_server) {
            /* No verifier round trip: attest against a local random nonce */
            if (get_random_nonce(local_nonce, sizeof(local_nonce)) != 0)
                errx(1, "Failed to generate a local nonce");
            nonce = local_nonce;
            nonce_sz = sizeof(local_nonce);
        } else {
            /* Connect to the server and establish a session */
            session = open_session();
            if (session == NULL) {
                printf("Failed to open session.\n");
                return 1;
            }
            nonce = (const uint8_t *)session->nonce;
            nonce_sz = session->nonce_size;
        }

        /* Request TA to issue evidence based on a given nonce */
        /* The buffer allocated here must be large enough to hold the CBOR
         * evidece */
        uint8_t cbor_evidence[1024] = {0};
        TEEC_Operation op = {0};

        if (packed_key_param && packed_key_param_len > 0) {
            /* Params: nonce(in), output(out), packed_key(in) */
            op.paramTypes = TEEC_PARAM_TYPES(
                TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
                TEEC_MEMREF_TEMP_INPUT, TEEC_NONE);
        } else {
            /* Params: nonce(in), output(out) */
            op.paramTypes = TEEC_PARAM_TYPES(
                TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
                TEEC_NONE, TEEC_NONE);
        }
        op.params[0].tmpref.buffer = (uint8_t *)nonce;
        op.params[0].tmpref.size = nonce_sz;
        op.params[1].tmpref.buffer = cbor_evidence;
        op.params[1].tmpref.size = sizeof(cbor_evidence);
        /* param[2] is packed key: PubX(32) || PubY(32) || key_blob(N) */
        if (packed_key_param && packed_key_param_len > 0) {
            op.params[2].tmpref.buffer = packed_key_param;
            op.params[2].tmpref.size = packed_key_param_len;
        }

        printf("\nInvoke TA.\n");
        t0 = ra_perf_now_us();
        res = TEEC_InvokeCommand(&sess,
                                 TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE,
                                 &op, &err_origin);
        if (res != TEEC_SUCCESS)
            errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
                 res, err_origin);
        ra_perf_log("evidence_get", ra_perf_now_us() - t0);

        printf("Invoked TA successfully.\n\n\n");

        /* Receive CBOR(COSE) evidence from PTA */
        printf("Received evidence of CBOR (COSE) format from PTA.\n\n");

        t0 = ra_perf_now_us();
        printf("CBOR(COSE) size: %ld\n", op.params[1].tmpref.size);
        printf("CBOR(COSE): ");
        print_binary_in_hex(op.params[1].tmpref.buffer,
                            op.params[1].tmpref.size);
        printf("\n\n");
        ra_perf_log("print_evidence", ra_perf_now_us() - t0);

        /* Send the generated evidence to the session just established */
        if (!no_server)
            post_evidence(session, op.params[1].tmpref.buffer,
                          op.params[1].tmpref.size);

        ra_perf_log("total", ra_perf_now_us() - t_total);
    }

    if (packed_key_param && packed_key_param != host_key)
        free(packed_key_param);
    if (host_key) free(host_key);
    if (host_pub_x) free(host_pub_x);
    if (host_pub_y) free(host_pub_y);

    return 0;
}
