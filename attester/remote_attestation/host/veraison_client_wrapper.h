#include <stdint.h>
#include <stdlib.h>

/**
 * C-compatible enum representation of the error enum from the Rust client.
 */
typedef enum VeraisonResult {
  Ok = 0,
  ConfigError,
  ApiError,
  CallbackError,
  NotImplementedError,
  DataConversionError,
} VeraisonResult;

/**
 * This structure represents an active challenge-response API session.
 *
 * Instances of this structure are obtained from the [`open_challenge_response_session()`] function,
 * and must always be disposed using [`free_challenge_response_session()`].
 *
 * The fields in this structure are read-only. The pointers provide access to data that is being
 * made visible to C client code, but is managed internally with Rust data structures. The pointers
 * are safe for C code to dereference and read, but not to mutate or retain beyond the lifetime
 * of the client session.
 */
typedef struct ChallengeResponseSession {
  /**
   * Pointer to a NUL-terminated string containing the session URL.
   */
  const char *session_url;
  /**
   * The number of bytes in the nonce challenge.
   */
  size_t nonce_size;
  /**
   * A pointer to the data buffer containing the nonce challenge bytes.
   */
  const uint8_t *nonce;
  /**
   * The number of accepted media types for evidence.
   */
  size_t accept_type_count;
  /**
   * An array of NUL-terminated strings specifying the accepted media types.
   */
  const char *const *accept_type_list;
  /**
   * A pointer to a NUL-terminated string containing the raw attestation result evidence.
   */
  const char *attestation_result;
  /**
   * A pointer to a NUL-terminated text string containing a logging/error message.
   */
  const char *message;
  /**
   * This field is a reserved pointer to Rust-managed data and must not be used by C client
   * code.
   */
  void *session_wrapper;
} ChallengeResponseSession;

/**
 * Establish a new challenge-response API session with the server.
 */
enum VeraisonResult open_challenge_response_session(const char *new_session_url,
                                                    size_t nonce_size,
                                                    const uint8_t *nonce,
                                                    struct ChallengeResponseSession **out_session);

/**
 * Execute a synchronous challenge-response operation using the given session.
 */
enum VeraisonResult challenge_response(struct ChallengeResponseSession *session,
                                       size_t evidence_size,
                                       const uint8_t *evidence,
                                       const char *media_type);

/**
 * Dispose of the challenge-response session.
 */
void free_challenge_response_session(struct ChallengeResponseSession *session);
