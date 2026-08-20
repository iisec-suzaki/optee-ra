/*
 * Performance measurement logging for the attestation host client.
 *
 * Enabled at runtime with --perf or the RA_PERF=1 environment variable.
 * Durations come from clock_gettime(CLOCK_MONOTONIC) and are printed to
 * stdout as:
 *
 *   RA_PERF|host|<event>|<duration_us>|key=<embedded|plain|black|->
 */
#ifndef HOST_RA_PERF_H
#define HOST_RA_PERF_H

#include <stdint.h>

extern int ra_perf_enabled;
extern const char *ra_perf_keymode;

uint64_t ra_perf_now_us(void);
void ra_perf_log(const char *event, uint64_t duration_us);

#endif /* HOST_RA_PERF_H */
