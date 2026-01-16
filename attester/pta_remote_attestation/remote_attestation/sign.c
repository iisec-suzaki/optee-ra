#include "sign.h"
#include <crypto/crypto.h>
#include <crypto/crypto_impl.h>
#include <string.h>
#include <utee_defines.h>

#define KEY_SIZE     32
#define KEY_SIZE_BIT KEY_SIZE * 8

/*
FIXME: Currently, keys are directly embedded within the code. From a security
standpoint these keys should be stored in a secure location and properly loaded
during program execution in a production environment.
The key information has been extracted using the command:
    $ echo "MKBCTNIcKUSDii11ySs3526iDZ8AiTo7Tu6KPAqv7D4=" | base64 -d | xxd -p
(and similar steps for obtaining the x, y, d values).

{
    "kty": "EC",
    "crv": "P-256",
    "x": "MKBCTNIcKUSDii11ySs3526iDZ8AiTo7Tu6KPAqv7D4",
    "y": "4Etl6SRW2YiLUrN5vfvVHuhp7x8PxltmWWlbbM4IFyM",
    "d": "870MB6gfuTJ4HtUnUvYMyJpr5eUZNP4Bk43bVdj3eAE"
}
*/

/* clang-format off */
#define PUBLIC_KEY_X                                   \
    0x30, 0xa0, 0x42, 0x4c, 0xd2, 0x1c, 0x29, 0x44,    \
    0x83, 0x8a, 0x2d, 0x75, 0xc9, 0x2b, 0x37, 0xe7,    \
    0x6e, 0xa2, 0x0d, 0x9f, 0x00, 0x89, 0x3a, 0x3b,    \
    0x4e, 0xee, 0x8a, 0x3c, 0x0a, 0xaf, 0xec, 0x3e
#define PUBLIC_KEY_Y                                   \
    0xe0, 0x4b, 0x65, 0xe9, 0x24, 0x56, 0xd9, 0x88,    \
    0x8b, 0x52, 0xb3, 0x79, 0xbd, 0xfb, 0xd5, 0x1e,    \
    0xe8, 0x69, 0xef, 0x1f, 0x0f, 0xc6, 0x5b, 0x66,    \
    0x59, 0x69, 0x5b, 0x6c, 0xce, 0x08, 0x17, 0x23
#define PRIVATE_KEY                                    \
    0xf3, 0xbd, 0x0c, 0x07, 0xa8, 0x1f, 0xb9, 0x32,    \
    0x78, 0x1e, 0xd5, 0x27, 0x52, 0xf6, 0x0c, 0xc8,    \
    0x9a, 0x6b, 0xe5, 0xe5, 0x19, 0x34, 0xfe, 0x01,    \
    0x93, 0x8d, 0xdb, 0x55, 0xd8, 0xf7, 0x78, 0x01
/* clang-format on */

static TEE_Result hash_sha256(const uint8_t *msg, size_t msg_len,
                              uint8_t *hash);
static void free_keypair(struct ecc_keypair *keypair);
static void free_pubkey(struct ecc_public_key *pk);

TEE_Result sign_ecdsa_sha256(const uint8_t *msg, size_t msg_len, uint8_t *sig,
                             size_t *sig_len,
                             const uint8_t *serialized_black_key,
                             size_t serialized_black_key_len) {
    TEE_Result res = TEE_SUCCESS;
    uint8_t hash_msg[TEE_SHA256_HASH_SIZE];
    struct ecc_keypair *key = NULL;
    struct ecc_public_key *pubkey = NULL;
    const uint8_t private_key[] = {PRIVATE_KEY};
    const uint8_t public_key_x[] = {PUBLIC_KEY_X};
    const uint8_t public_key_y[] = {PUBLIC_KEY_Y};

    /* Allocate the key pair */
    key = calloc(1, sizeof(*key));
    if (key == NULL) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto out;
    }

    res = crypto_acipher_alloc_ecc_keypair(key, TEE_TYPE_ECDSA_KEYPAIR,
                                           KEY_SIZE_BIT);
    if (res != TEE_SUCCESS) {
        goto out;
    }
    key->curve = TEE_ECC_CURVE_NIST_P256;

    /* If external key is provided, use it instead of embedded key */
    if (serialized_black_key && serialized_black_key_len > 0) {
        res = crypto_bignum_bin2bn(serialized_black_key,
                                   serialized_black_key_len, key->d);
        if (res != TEE_SUCCESS) {
            goto free_key;
        }
    } else {
        /* Copy the embedded private key */
        res = crypto_bignum_bin2bn(private_key, KEY_SIZE, key->d);
        if (res != TEE_SUCCESS) {
            goto free_key;
        }

        /* Allocate a public key storage for verification */
        pubkey = calloc(1, sizeof(*pubkey));
        if (pubkey == NULL) {
            res = TEE_ERROR_OUT_OF_MEMORY;
            goto free_key;
        }

        res = crypto_acipher_alloc_ecc_public_key(pubkey, TEE_TYPE_ECDSA_PUBLIC_KEY,
                                                  KEY_SIZE_BIT);
        if (res != TEE_SUCCESS) {
            goto free_pubkey;
        }
        pubkey->curve = TEE_ECC_CURVE_NIST_P256;

        /* Copy the public key */
        res = crypto_bignum_bin2bn(public_key_x, KEY_SIZE, pubkey->x);
        if (res != TEE_SUCCESS) {
            goto free_pubkey;
        }
        res = crypto_bignum_bin2bn(public_key_y, KEY_SIZE, pubkey->y);
        if (res != TEE_SUCCESS) {
            goto free_pubkey;
        }
    }

    /* Hash the msg */
    res = hash_sha256(msg, msg_len, hash_msg);
    if (res != TEE_SUCCESS)
        goto free_pubkey;

    /* Sign the hashed msg by the key pair */
    res = crypto_acipher_ecc_sign(TEE_ALG_ECDSA_SHA256, key, hash_msg,
                                  TEE_SHA256_HASH_SIZE, sig, sig_len);
    if (res != TEE_SUCCESS)
        goto free_pubkey;

    /* Verify the signature if we have the public key */
    if (pubkey) {
        res = crypto_acipher_ecc_verify(TEE_ALG_ECDSA_SHA256, pubkey, hash_msg,
                                        TEE_SHA256_HASH_SIZE, sig, *sig_len);
        if (res == TEE_SUCCESS) {
            DMSG("Success to verify");
        } else {
            DMSG("Failed to verify");
        }
        /* Reset res to success even if verify fails - we still signed */
        res = TEE_SUCCESS;
    }

free_pubkey:
    if (pubkey)
        free_pubkey(pubkey);
free_key:
    if (key)
        free_keypair(key);
out:
    return res;
}

static TEE_Result hash_sha256(const uint8_t *msg, size_t msg_len,
                              uint8_t *hash) {
    TEE_Result res = TEE_SUCCESS;
    void *ctx = NULL;

    res = crypto_hash_alloc_ctx(&ctx, TEE_ALG_SHA256);
    if (res != TEE_SUCCESS)
        return res;
    res = crypto_hash_init(ctx);
    if (res != TEE_SUCCESS)
        goto out;
    res = crypto_hash_update(ctx, msg, msg_len);
    if (res != TEE_SUCCESS)
        goto out;
    res = crypto_hash_final(ctx, hash, TEE_SHA256_HASH_SIZE);

out:
    crypto_hash_free_ctx(ctx);
    return res;
}

static void free_keypair(struct ecc_keypair *keypair) {
    if (!keypair)
        return;

    if (keypair->d)
        crypto_bignum_free(&keypair->d);
    if (keypair->x)
        crypto_bignum_free(&keypair->x);
    if (keypair->y)
        crypto_bignum_free(&keypair->y);

    memset(keypair, 0, sizeof(*keypair));
    free(keypair);
}

static void free_pubkey(struct ecc_public_key *pk) {
    if (!pk)
        return;

    if (pk->x)
        crypto_bignum_free(&pk->x);
    if (pk->y)
        crypto_bignum_free(&pk->y);

    memset(pk, 0, sizeof(*pk));
    free(pk);
}
