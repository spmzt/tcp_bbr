#include "cc.h"
#include "helper.h"

/* Per RFC5681 Section 3.1 */
static int min_cwnd_rwnd(struct tcp_cb *cb) {
    if (cb->cwnd <= cb->rwnd)
        return cb->cwnd;
    return cb->rwnd;
}

/* Per RFC5681 Section 3.1 */
static int initial_window(struct tcp_cb *cb) {
    if (cb->smss > 2190)
        return (2 * cb->smss);
    else if (cb->smss > 1095)
        return (3 * cb->smss);
    else
        return (4 * cb->smss);
};

/* Per RFC5681 Section 3.1 */
static void initial_sshthresh(struct tcp_cb *cb) {
    cb->ssthresh = cb->cwnd;
};

static void cc_ack_recv(struct tcp_cb *cb) {
    uint32_t this_bytes_ack = cb->snd_una - cb->bytes_acked;
    if (cb->cwnd > cb->ssthresh)
        /*  If the above formula yields 0, the result SHOULD be rounded up to 1 byte. */
        cb->cwnd += max(cb->smss*cb->smss/cb->cwnd, 1); 
    else
        cb->cwnd += min(this_bytes_ack, cb->smss);
}

/* Unacknowledged Sequence in Flight */
static int tcp_compute_pipe(struct tcp_cb *cb) {
    return (cb->snd_seq_max - cb->snd_una);
}

/* 
 * tcp_compute_pipe(cb) * cb->cwnd should be flight size.
 * FlightSize is the amount of outstanding data in the network.
 */
static void cc_cong_signal(struct tcp_cb *cb) {
    cb->ssthresh = max((tcp_compute_pipe(cb) * cb->cwnd) / 2, 2*cb->smss);
}