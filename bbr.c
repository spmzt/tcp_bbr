#include "bbr.h"
#include "cc.h"
#include <bits/types.h>
#include <stdint.h>
#include <time.h>

#define true 1
#define false 0

/*
 * A constant specifying the minimum gain value for calculating the pacing rate that will
 * allow the sending rate to double each round (4 * ln(2) ~= 2.77) [BBRStartupPacingGain];
 * used in Startup mode for BBR.pacing_gain.
 */
#define BBRStartupPacingGain 2.77

/*
 * A constant specifying the minimum gain value that allows the sending rate to double each round (2) [BBRStartupCwndGain].
 * Used by default in most phases for BBR.cwnd_gain.
 */
#define BBRDefaultCwndGain 2.3

/* Get time */
static uint64_t Now() {
    return (int long)time(NULL);
}

/* Check if new measurement updates the 1st, 2nd or 3rd choice max. */
static uint32_t UpdateWindowedMaxFilter(struct MaxBwFilter *m, uint32_t win, uint32_t value, uint32_t time) {
    struct minmax_sample val = { .t = time, .v = value };

    if (val.v >= m->s[0].v || val.t - m->s[2].t > win) /* found new max or nothing left in window (nothing left in window? i cant understand) */
        return minmax_reset(m, time, value); /* forget earlier samples */

    if (val.v >= m->s[1].v)
        m->s[1] = val;

    return minmax_subwin_update(m, win, &val);
}

static void InitWindowedMaxFilter(struct MaxBwFilter *m, uint32_t value, uint32_t time) {
    struct minmax_sample val = { .t = time, .v = value };
    m->s[2] = m->s[1] = m->s[0] = val;
}

static void BBRResetCongestionSignals(struct tcp_bbr *BBR) {
    BBR->loss_in_round = 0;
    BBR->bw_latest = 0;
    BBR->inflight_latest = 0;
}

/*
 * When transitioning out of ProbeRTT, BBR calls BBRResetLowerBounds() to reset the lower bounds,
 * since any congestion encountered in ProbeRTT may have pulled the short-term model far below the capacity of the path.
 */
static void BBRResetLowerBounds(struct tcp_bbr *BBR) {
    BBR->bw_lo       = Infinity;
    BBR->inflight_lo = Infinity;
}

/* Upon connection initialization */
static void BBRInitRoundCounting(struct tcp_bbr *BBR) {
    BBR->next_round_delivered = 0;
    BBR->round_start = false;
    BBR->round_count = 0;
}

/* Upon starting a full pipe detection process, the following initialization runs */
static void BBRResetFullBW(struct tcp_bbr *BBR) {
    BBR->full_bw = 0;
    BBR->full_bw_count = 0;
    BBR->full_bw_now = 0;
}

static void BBRInitPacingRate(struct tcp_bbr *BBR, struct tcp_cb *tp) {
    uint32_t InitialCwnd = initial_window(tp);

    uint32_t nominal_bandwidth = InitialCwnd / (tp->SRTT ? tp->SRTT : 1); /* 1 is for 1 ms */
    BBR->pacing_rate =  BBRStartupPacingGain * nominal_bandwidth;
}

/*
 * When initializing a connection, or upon any later entry into Startup mode,
 * BBR executes the following BBREnterStartup() steps
 */
static void BBREnterStartup(struct tcp_bbr *BBR) {
    BBR->state = STARTUP;
    BBR->pacing_gain = BBRStartupPacingGain;
    BBR->cwnd_gain = BBRDefaultCwndGain;
};

/*
 * bbr_init on linux implementation.
 * Upon transport connection initialization,
 * BBR executes its initialization steps
 */
static void BBROnInit(struct tcp_cb *tp) {
    struct minmax_sample m = {0};
    struct tcp_bbr BBR = {.MaxBwFilter = {0}};

    InitWindowedMaxFilter(&BBR.MaxBwFilter, 0, .0);
    BBR.min_rtt = tp->SRTT ? tp->SRTT : 1;
    BBR.min_rtt_stamp = Now();
    BBR.probe_rtt_done_stamp = 0;
    BBR.probe_rtt_round_done = false;
    BBR.prior_cwnd = 0;
    BBR.idle_restart = false;
    BBR.extra_acked_interval_start = Now();
    BBR.extra_acked_delivered = 0;
    BBR.full_bw_reached = false;
    BBRResetCongestionSignals(&BBR);
    BBRResetLowerBounds(&BBR);
    BBRInitRoundCounting(&BBR);
    BBRResetFullBW(&BBR);
    BBRInitPacingRate(&BBR, tp);
    BBREnterStartup(&BBR);
};

static void BBRCheckStartupHighLoss() {

}

static void BBREnterDrain() {

}

static void BBRCheckStartupDone(struct tcp_bbr *BBR) {
    BBRCheckStartupHighLoss();
    if (BBR->state == STARTUP && BBR->full_bw_reached)
        BBREnterDrain();
};

static void BBRUpdateRound(struct tcp_bbr) {

}