#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include "cc.h"

#define Infinity    UINT_MAX;

#define CYCLE_LEN	8	/* number of phases in a pacing gain cycle */

/* Window length of bw filter (in rounds): */
static const int bbr_bw_rtts = CYCLE_LEN + 2;

/* BBR has the following modes for deciding how fast to send: */
enum bbr_mode {
	STARTUP,	/* ramp up sending rate rapidly to fill pipe */
	DRAIN,	/* drain any queue created during startup */
	PROBE_BW,	/* discover, share bw: pace around estimated bw */
	PROBE_RTT,	/* cut inflight to min to probe min_rtt */
};

/* bbr probe_bw modes */
enum bbr_bw_mode {
    PROBE_BW_DOWN,
    PROBE_BW_CRUISE,
    PROBE_BW_REFILL,
    PROBE_BW_UP,
};

/* bbr ack phases */
enum bbr_ack_phases {
    ACKS_PROBE_STARTING, /* Probe BW UP */
    ACKS_REFILLING, /* Probe BW REFILL */
    ACKS_PROBE_STOPPING, /* Probe BW DOWN */
    ACKS_PROBE_FEEDBACK, /* starting to get bw probing samples */
};

/* A single data point for our parameterized min-max tracker */
struct minmax_sample {
	uint32_t t;	/* time measurement was taken */
	uint32_t v;	/* value measured */
};

struct MaxBwFilter {
    struct minmax_sample s[2];
};

struct tcp_bbr {
    struct tcp_cb *C; /* The tcp control block lock */
    struct MaxBwFilter MaxBwFilter;
    
    uint64_t bw_probe_wait; /* how long to wait until probing for bandwidth by between 2-3 seconds in usec */
    uint64_t min_rtt; /* Estimated Minimum Round-Trip Time */
    uint64_t min_rtt_stamp; /* The wall clock time at which the current BBR.min_rtt sample was obtained */
    uint64_t probe_rtt_done_stamp; /* end time for BBR_PROBE_RTT mode */
    uint64_t probe_rtt_min_stamp; /* The wall clock time at which the current BBR.probe_rtt_min_delay sample was obtained. */
    uint64_t extra_acked_interval_start; /* the start of the time interval for estimating the excess amount of data acknowledged due to aggregation effects. */
    uint32_t extra_acked_delivered; /* the volume of data marked as delivered since BBR.extra_acked_interval_start. */
    uint32_t prior_cwnd; /* prior cwnd upon entering loss recovery */
    uint32_t bw; /* he maximum sending bandwidth that the algorithm estimates is appropriate for matching the current network path delivery rate, given all available signals in the model, at any time scale. It is the min() of max_bw and bw_lo. */
    uint32_t max_bw; /* the full bandwidth available to the flow */
    uint32_t bw_latest; /* a 1-round-trip max of delivered bandwidth (rs.delivery_rate). */
    uint32_t inflight_latest; /* a 1-round-trip max of delivered volume of data (rs.delivered) */
    uint32_t bw_lo; /* lower 32 bits of bw */
    uint32_t next_round_delivered; /* packet.delivered value denoting the end of a packet-timed round trip. */
    uint32_t round_count; /* Count of packet-timed round trips elapsed so far. */

    uint32_t full_bw; /* A recent baseline BBR.max_bw to estimate if BBR has "filled the pipe" in Startup. */
    uint32_t full_bw_count; /* The number of non-app-limited round trips without large increases in BBR.full_bw. */

    uint32_t pacing_rate; /* The current pacing rate for a BBR flow, which controls inter-packet spacing. */

    uint32_t pacing_gain; /* The dynamic gain factor used to scale BBR.bw to produce BBR.pacing_rate. */
    uint32_t cwnd_gain; /* The dynamic gain factor used to scale the estimated BDP to produce a congestion window (cwnd). */

    /*
     * Analogous to BBR.bw_lo,
     * the short-term maximum volume of in-flight data that the algorithm estimates is safe for matching the current network path delivery process,
     * based on any loss signals in the current bandwidth probing cycle.
     * This is generally lower than max_inflight or inflight_hi (thus the name). (Part of the short-term model.)
     */
    uint32_t inflight_lo;

    /* unkwown variables */
    uint64_t cycle_stamp; /* the probe bw wall clock */
    uint bw_probe_up_rounds;
    uint bw_probe_up_acks;
    uint probe_up_cnt;
    uint rounds_since_bw_probe;

    uint16_t full_bw_reached:1, /* reached full bw in Startup? */
            probe_rtt_round_done:1,
            idle_restart:1, /* restarting after idle? */
            loss_in_round:1, /* first loss in this round trip? */
            round_start:1, /* A boolean that BBR sets to true once per packet-timed round trip, on ACKs that advance BBR.round_count. */
            full_bw_now:1, /* A boolean that records whether BBR estimates that it has fully utilized its available bandwidth since it most recetly started looking. */
            state:2, /* bbr_mode */
            sub_state:2, /* bbr sub state of Probe_BW */
            ack_phase:2, /* bbr ack phases */
            unused:4;
};

static inline uint32_t
minmax_reset(struct MaxBwFilter *m, uint32_t time, uint32_t value)
{
	struct minmax_sample val = { .t = time, .v = value };

	m->s[1] = m->s[0] = val;
	return m->s[0].v;
}

/* As time advances, update the 1st and 2nd choices. */
static uint32_t
minmax_subwin_update(struct MaxBwFilter *m, uint32_t win, const struct minmax_sample *val)
{
    uint32_t dt = val->t - m->s[0].t;

    if (dt > win) {
		/*
		 * Passed entire window without a new val so make 2nd
		 * choice the new val.
		 * we may have to iterate this since our 2nd choice
		 * may also be outside the window.
		 */
		m->s[0] = m->s[1];
		m->s[1] = *val;
		if (val->t - m->s[0].t > win) {
			m->s[0] = m->s[1];
			m->s[1] = *val;
		}
	} else if (m->s[1].t == m->s[0].t && dt > win/4) {
		/*
		 * We've passed a quarter of the window without a new val
		 * so take a 2nd choice from the 2nd quarter of the window.
		 */
		m->s[1] = *val;
    }
	return m->s[0].v;
}