/*
 * Performance measurement logging for the remote attestation PTA.
 *
 * Enabled with CFG_REMOTE_ATTESTATION_PERF=y. Durations are taken from the
 * ARM generic timer (CNTPCT via barrier_read_counter_timer()) and reported
 * in microseconds as:
 *
 *   RA_PERF|pta|<event>|<duration_us>|key=<embedded|plain|black|->
 *
 * Events are buffered with ra_perf_record() and emitted in one batch by
 * ra_perf_flush(). Buffering matters: the secure console UART is synchronous
 * and slow, so printing between two measured operations would inflate the
 * later measurements.
 */
#ifndef PTA_REMOTE_ATTESTATION_PERF_H
#define PTA_REMOTE_ATTESTATION_PERF_H

#ifdef CFG_REMOTE_ATTESTATION_PERF

#include <arm.h>
#include <stddef.h>
#include <stdint.h>

static inline uint64_t ra_perf_now(void)
{
    return barrier_read_counter_timer();
}

/*
 * Key-path label used to split measurement runs in the logs:
 *   embedded = no external key, embedded test key
 *   plain    = external plain private key (32 bytes)
 *   black    = external serialized CAAM black key (> 32 bytes)
 * Note: on i.MX with CFG_NXP_CAAM_ECC_DRV all three paths sign on CAAM
 * hardware; "black" additionally pays blob decapsulation + CCM key import.
 */
static inline const char *ra_perf_keymode(const uint8_t *key, size_t key_len)
{
    if (!key || !key_len)
        return "embedded";
    return key_len > 32 ? "black" : "plain";
}

void ra_perf_reset(void);
void ra_perf_record(const char *event, uint64_t start_ticks,
                    uint64_t end_ticks, const char *keymode);
void ra_perf_flush(void);

#define RA_PERF_DECL(v) uint64_t v = 0
#define RA_PERF_RESET() ra_perf_reset()
#define RA_PERF_START(v) ((v) = ra_perf_now())
#define RA_PERF_STOP(v, event, keymode) \
    ra_perf_record((event), (v), ra_perf_now(), (keymode))
#define RA_PERF_FLUSH() ra_perf_flush()

#else /* CFG_REMOTE_ATTESTATION_PERF */

#define RA_PERF_DECL(v)
#define RA_PERF_RESET() \
    do { \
    } while (0)
#define RA_PERF_START(v) \
    do { \
    } while (0)
#define RA_PERF_STOP(v, event, keymode) \
    do { \
    } while (0)
#define RA_PERF_FLUSH() \
    do { \
    } while (0)

#endif /* CFG_REMOTE_ATTESTATION_PERF */

#endif /* PTA_REMOTE_ATTESTATION_PERF_H */
