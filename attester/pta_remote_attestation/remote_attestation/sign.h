#ifndef PTA_REMOTE_ATTESTATION_SIGN_H
#define PTA_REMOTE_ATTESTATION_SIGN_H

#include <stddef.h>
#include <stdint.h>
#include <tee_api_types.h>

/**
  Sign a message with ECDSA w/ SHA-256
  @param msg       The message to sign
  @param msg_len   The length of the message to sign
  @param sig       [out] Where to store the signature. The signature format follows the 
                   specifications in RFC 7518 Section 3.4. This means the signature will be 
                   output in a 'plain signature' format, diverging from the traditional ASN.1 
                   DER encoding. In this context, 'plain signature' refers to the direct 
                   concatenation of the r and s values of the ECDSA signature, each occupying 
                   exactly half of the signature space. When using a 256-bit ECDSA key, r and
                   s are each 32 bytes long. In a plain signature, these values are simply
                   concatenated to produce a total signature of 64 bytes.
  @param sig_len   [in/out] The max size and resulting size of the signature. It is important 
                   to ensure that the provided buffer is sufficiently large to hold the 
                   signature in its specified format. The resulting size will indicate the 
                   actual size of the signature in bytes.
  @param serialized_black_key      [in] CAAM black key for signing (NULL to use default key)
  @param serialized_black_key_len  [in] Length of the serialized black key
  @return TEE_SUCCESS if successful
*/
TEE_Result sign_ecdsa_sha256(const uint8_t *msg, size_t msg_len, uint8_t *sig,
                             size_t *sig_len,
                             const uint8_t *serialized_black_key,
                             size_t serialized_black_key_len);

/**
 * Compute PSA instance-id from a signing key's public coordinates.
 *
 * Per the PSA Attestation Token specification (draft-tschofenig-rats-psa-token,
 * Section 4.2.1), the Instance ID claim is a UEID of type ECDSA as defined in
 * Entity Attestation Token (EAT, RFC 9711, Section 4.2.1):
 *
 *   instance_id = 0x01 || SHA-256(0x04 || PubX || PubY)
 *
 * - 0x01: EAT UEID type byte indicating ECDSA
 * - 0x04 || PubX || PubY: SEC 1 uncompressed EC point encoding
 *
 * References:
 *   https://datatracker.ietf.org/doc/draft-tschofenig-rats-psa-token/
 *   https://www.rfc-editor.org/rfc/rfc9711.html#section-4.2.1
 *
 * @param pub_x        32-byte X coordinate of the public key
 * @param pub_y        32-byte Y coordinate of the public key
 * @param instance_id  [out] 33-byte buffer (1-byte prefix + 32-byte hash)
 * @return TEE_SUCCESS if successful
 */
TEE_Result compute_instance_id(const uint8_t *pub_x, const uint8_t *pub_y,
                               uint8_t *instance_id);

/**
 * Get the embedded test key's public coordinates.
 * @param pub_x  [out] 32-byte buffer for X coordinate
 * @param pub_y  [out] 32-byte buffer for Y coordinate
 * @return TEE_SUCCESS
 */
TEE_Result get_test_key_pubkey(uint8_t *pub_x, uint8_t *pub_y);

#endif /*PTA_REMOTE_ATTESTATION_SIGN_H*/
