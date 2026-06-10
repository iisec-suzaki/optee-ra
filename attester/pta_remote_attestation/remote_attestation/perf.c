#include "perf.h"

#include <inttypes.h>
#include <trace.h>

#define RA_PERF_MAX_EVENTS 16

struct ra_perf_event {
    const char *event;
    const char *keymode;
    uint64_t ticks;
};

/*
 * Simple per-command event buffer. Measurement runs are expected to be
 * sequential (one attestation client at a time); concurrent PTA invocations
 * would interleave their events.
 */
static struct ra_perf_event ra_perf_events[RA_PERF_MAX_EVENTS];
static size_t ra_perf_num_events;

void ra_perf_reset(void)
{
    ra_perf_num_events = 0;
}

void ra_perf_record(const char *event, uint64_t start_ticks,
                    uint64_t end_ticks, const char *keymode)
{
    struct ra_perf_event *e = NULL;

    if (ra_perf_num_events >= RA_PERF_MAX_EVENTS)
        return;

    e = &ra_perf_events[ra_perf_num_events++];
    e->event = event;
    e->keymode = keymode;
    e->ticks = end_ticks - start_ticks;
}

void ra_perf_flush(void)
{
    uint32_t freq = read_cntfrq();
    uint64_t flush_start = ra_perf_now();
    size_t i = 0;

    /* Report the counter frequency once so tick math can be verified */
    IMSG("RA_PERF|pta|cntfrq|%" PRIu32 "|key=-", freq);

    for (i = 0; i < ra_perf_num_events; i++) {
        struct ra_perf_event *e = &ra_perf_events[i];
        uint64_t us = e->ticks * 1000000ULL / freq;

        IMSG("RA_PERF|pta|%s|%" PRIu64 "|key=%s", e->event, us, e->keymode);
    }
    ra_perf_num_events = 0;

    /*
     * Console writes are synchronous, so this flush is visible in the
     * brackets measured by the layers above (TA pta_invoke, host
     * evidence_get). Report its own cost so it can be subtracted; only
     * the final line below remains unaccounted for.
     */
    IMSG("RA_PERF|pta|perf_flush|%" PRIu64 "|key=-",
         (ra_perf_now() - flush_start) * 1000000ULL / freq);
}
