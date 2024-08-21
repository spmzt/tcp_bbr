#include "bbr.h"
#include "cc.h"
#include "helper.h"
#include <bits/types.h>
#include <stdint.h>
#include <time.h>

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

/*
 * The static discount factor of 1% used to scale BBR.bw to produce BBR.pacing_rate.
 * To help drive the network toward lower queues and low latency while maintaining high utilization,
 * the BBRPacingMarginPercent constant of 1 aims to cause BBR to pace at 1% below the bw, on average.
 */
#define BBRPacingMarginPercent 1


/* Get time */
static uint64_t
Now()
{
    return (int long)time(NULL);
}

/* Check if new measurement updates the 1st, 2nd or 3rd choice max. */
static uint32_t
UpdateWindowedMaxFilter(struct MaxBwFilter *m, uint32_t win, uint32_t value, uint32_t time)
{
    struct minmax_sample val = { .t = time, .v = value };

    if (val.v >= m->s[0].v || val.t - m->s[2].t > win) /* found new max or nothing left in window (nothing left in window? i cant understand) */
        return minmax_reset(m, time, value); /* forget earlier samples */

    if (val.v >= m->s[1].v)
        m->s[1] = val;

    return minmax_subwin_update(m, win, &val);
}

static void
InitWindowedMaxFilter(struct MaxBwFilter *m, uint32_t value, uint32_t time)
{
    struct minmax_sample val = { .t = time, .v = value };
    m->s[2] = m->s[1] = m->s[0] = val;
}

static void
BBRResetCongestionSignals(struct tcp_bbr *BBR)
{
    BBR->loss_in_round = 0;
    BBR->bw_latest = 0;
    BBR->inflight_latest = 0;
}

/*
 * When transitioning out of ProbeRTT, BBR calls BBRResetLowerBounds() to reset the lower bounds,
 * since any congestion encountered in ProbeRTT may have pulled the short-term model far below the capacity of the path.
 */
static void
BBRResetLowerBounds(struct tcp_bbr *BBR)
{
    BBR->bw_lo       = Infinity;
    BBR->inflight_lo = Infinity;
}

/* Upon connection initialization */
static void
BBRInitRoundCounting(struct tcp_bbr *BBR)
{
    BBR->next_round_delivered = 0;
    BBR->round_start = false;
    BBR->round_count = 0;
}

/* Upon starting a full pipe detection process, the following initialization runs */
static void
BBRResetFullBW(struct tcp_bbr *BBR)
{
    BBR->full_bw = 0;
    BBR->full_bw_count = 0;
    BBR->full_bw_now = 0;
}

/*
 * When a BBR flow starts it has no bw estimate (bw is 0).
 * So in this case it sets an initial pacing rate based on the transport sender implementation's initial congestion window ("InitialCwnd", e.g. from [RFC6928]),
 * the initial SRTT (smoothed round-trip time) after the first non-zero RTT sample, and the initial pacing_gain
 */
static void
BBRInitPacingRate(struct tcp_bbr *BBR)
{
    uint32_t InitialCwnd = initial_window(BBR->C);

    uint32_t nominal_bandwidth = InitialCwnd / (BBR->C->SRTT ? BBR->C->SRTT : 1); /* 1 is for 1 ms */
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
 * bbr_init on FreeBSD/Linux implementation.
 * Upon transport connection initialization,
 * BBR executes its initialization steps
 */
static void BBROnInit(struct tcp_cb *C) {
    struct tcp_bbr BBR = {.C = C, .MaxBwFilter = {0}};

    InitWindowedMaxFilter(&BBR.MaxBwFilter, 0, .0);
    BBR.min_rtt = C->SRTT ? C->SRTT : 1;
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
    BBRInitPacingRate(&BBR);
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

static uint8_t
IsInAProbeBWState(struct tcp_bbr *BBR)
{
    uint8_t state = BBR->state;
    return (state == PROBE_BW_DOWN ||
            state == PROBE_BW_CRUISE ||
            state == PROBE_BW_REFILL ||
            state == PROBE_BW_UP);
};


/*
 * After initialization, on each data ACK BBR updates its pacing rate to be proportional to bw,
 * as long as it estimates that it has filled the pipe (BBR.full_bw_reached is true; see the "Startup" section for details),
 * or doing so increases the pacing rate.
 * Limiting the pacing rate updates in this way helps the connection probe robustly for bandwidth until it estimates it has reached its full available bandwidth ("filled the pipe").
 * In particular, this prevents the pacing rate from being reduced when the connection has only seen application-limited bandwidth samples.
 * BBR updates the pacing rate on each ACK by executing the BBRSetPacingRate() step as follows
 */
static void
BBRSetPacingRateWithGain(struct tcp_bbr *BBR, uint32_t pacing_gain)
{
    uint32_t rate = pacing_gain * BBR->bw * (100 - BBRPacingMarginPercent) / 100;
    if (BBR->full_bw_reached || rate > BBR->pacing_rate)
      BBR->pacing_rate = rate;
};

/*
 * The BBRSaveCwnd() and BBRRestoreCwnd() helpers help remember and restore the last-known good cwnd (the latest cwnd unmodulated by loss recovery or ProbeRTT),
 * and is defined as follow
 */
static void
BBRSaveCwnd(struct tcp_bbr *BBR)
{
    if (!InLossRecovery() && BBR->state != PROBE_RTT)
      BBR->prior_cwnd = BBR->C->cwnd;
    else
      BBR->prior_cwnd = max(BBR->prior_cwnd, BBR->C->cwnd);
};

static void
BBRRestoreCwnd(struct tcp_bbr *BBR)
{
    BBR->C->cwnd = max(BBR->C->cwnd, BBR->prior_cwnd);
};

/*
 * Randomized decision about how long to wait until
 * probing for bandwidth, using round count and wall clock.
 */
static void
BBRPickProbeWait(struct tcp_bbr *BBR)
{
    /* Decide random round-trip bound for wait: */
    BBR->rounds_since_bw_probe = random_int_between(0, 1); /* 0 or 1 */
    /* Decide the random wall clock bound for wait: */
    BBR->bw_probe_wait = (2 + random_float_between_0_and_1()) * USECS_IN_SECOND; /* 0..1 sec */;
};

static void
BBRStartRound(struct tcp_bbr *BBR)
{
    BBR->next_round_delivered = BBR->C->delivered;
};

/* Raise inflight_hi slope if appropriate. */
static void
BBRRaiseInflightHiSlope(struct tcp_bbr *BBR)
{
    uint32_t growth_this_round = 1*BBR->C->smss << BBR->bw_probe_up_rounds;
    BBR->bw_probe_up_rounds = min(BBR->bw_probe_up_rounds + 1, 30);
    BBR->probe_up_cnt = max(BBR->C->cwnd / growth_this_round, 1);
};

static void
BBRStartProbeBW_DOWN(struct tcp_bbr *BBR)
{
    BBRResetCongestionSignals(BBR);
    BBR->probe_up_cnt = Infinity /* not growing inflight_hi */
    BBRPickProbeWait(BBR);
    BBR->cycle_stamp = Now();  /* start wall clock */
    BBR->ack_phase  = ACKS_PROBE_STOPPING;
    BBRStartRound(BBR);
    BBR->sub_state = PROBE_BW_DOWN;
};

static void
BBRStartProbeBW_CRUISE(struct tcp_bbr *BBR)
{
    BBR->sub_state = PROBE_BW_CRUISE;
};

static void
BBRStartProbeBW_REFILL(struct tcp_bbr *BBR)
{
    BBRResetLowerBounds(BBR);
    BBR->bw_probe_up_rounds = 0;
    BBR->bw_probe_up_acks = 0;
    BBR->ack_phase = ACKS_REFILLING;
    BBRStartRound(BBR);
    BBR->sub_state = PROBE_BW_REFILL;
};

static void
BBRStartProbeBW_UP(struct tcp_bbr *BBR)
{
    BBR->ack_phase = ACKS_PROBE_STARTING;
    BBRStartRound(BBR);
    BBRResetFullBW(BBR);
    BBR->full_bw = rs.delivery_rate; /* incomplete */
    BBR->state = PROBE_BW_UP;
    BBRRaiseInflightHiSlope(BBR);
};

static void
BBRExitProbeRTT(struct tcp_bbr *BBR)
{
    BBRResetLowerBounds(BBR);
    if (BBR->full_bw_reached)
    {
      BBRStartProbeBW_DOWN(BBR);
      BBRStartProbeBW_CRUISE(BBR);
    } else
      BBREnterStartup(BBR);
}

static void
BBRCheckProbeRTTDone(struct tcp_bbr *BBR)
{
    if (BBR->probe_rtt_done_stamp != 0 &&
        Now() > BBR->probe_rtt_done_stamp)
    {
        /* schedule next ProbeRTT: */
        BBR->probe_rtt_min_stamp = Now();
        BBRRestoreCwnd(BBR);
        BBRExitProbeRTT();
    }
};

/*
 * When restarting from idle in ProbeBW states, BBR leaves its cwnd as-is and paces packets at exactly BBR.bw,
 * aiming to return as quickly as possible to its target operating point of rate balance and a full pipe. Specifically,
 * if the flow's BBR.state is ProbeBW, and the flow is application-limited, and there are no packets in flight currently,
 * then before the flow sends one or more packets BBR sets BBR.pacing_rate to exactly BBR.bw.
 * Also, when restarting from idle BBR checks to see if the connection is in ProbeRTT and has met the exit conditions for ProbeRTT.
 * If a connection goes idle during ProbeRTT then often it will have met those exit conditions by the time it restarts,
 * so that the connection can restore the cwnd to its full value before it starts transmitting a new flight of data.
 * More precisely, the BBR algorithm takes the following steps in BBRHandleRestartFromIdle() before sending a packet for a flow.
 */
static void BBRHandleRestartFromIdle(struct tcp_bbr *BBR) {
    /* Check pipe bbr_state_startup in FreeBSD and bbr_check_full_bw_reached in linux */
    if (BBR->C->pipe == 0 && BBR->C->app_limited) {
        BBR->idle_restart = true;
        BBR->extra_acked_interval_start = Now();
        if (IsInAProbeBWState(BBR))
            BBRSetPacingRateWithGain(BBR, 1);
        else if (BBR->state == PROBE_RTT)
            BBRCheckProbeRTTDone(BBR);
    }
}

/* 
 * Per-Transmit Steps
 *
 When transmitting, BBR merely needs to check for the case where the flow is restarting from idle.
 */
static void BBROnTransmit() {
    BBRHandleRestartFromIdle();
}