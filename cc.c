#include "cc.h"
#include "helper.h"

/* Per RFC5681 Section 3.1 */
static int
min_cwnd_rwnd(struct tcp_cb *cb)
{
    if (cb->cwnd <= cb->rwnd)
        return cb->cwnd;
    return cb->rwnd;
}

/* Per RFC5681 Section 3.1 */
int
initial_window(struct tcp_cb *cb)
{
    if (cb->smss > 2190)
        return (2 * cb->smss);
    else if (cb->smss > 1095)
        return (3 * cb->smss);
    else
        return (4 * cb->smss);
};

/* Per RFC5681 Section 3.1 */
static void
initial_sshthresh(struct tcp_cb *cb)
{
    cb->ssthresh = cb->cwnd;
};

static void
cc_ack_recv(struct tcp_cb *cb)
{
    uint32_t this_bytes_ack = cb->snd_una - cb->bytes_acked;
    if (cb->cwnd > cb->ssthresh)
        /*  If the above formula yields 0, the result SHOULD be rounded up to 1 byte. */
        cb->cwnd += max(cb->smss*cb->smss/cb->cwnd, 1); 
    else
        cb->cwnd += min(this_bytes_ack, cb->smss);
}

/* Unacknowledged Sequence in Flight */
static int
tcp_compute_pipe(struct tcp_cb *cb) {
    return (cb->snd_max - cb->snd_una);
}

/* 
 * tcp_compute_pipe(cb) * cb->cwnd should be flight size.
 * FlightSize is the amount of outstanding data in the network.
 */
static void
cc_cong_signal(struct tcp_cb *cb) {
    cb->ssthresh = max((tcp_compute_pipe(cb) * cb->cwnd) / 2, 2*cb->smss);
}

uint32_t
ctf_outstanding(struct tcp_cb *tp)
{
	uint32_t bytes_out;

	bytes_out = tp->snd_max - tp->snd_una;
	if (tp->state < TCPS_ESTABLISHED)
		bytes_out++;
	if (tp->flags & TF_SENTFIN)
		bytes_out++;
	return (bytes_out);
}

uint32_t
ctf_flight_size(struct tcp_cb *tp, uint32_t rc_sacked)
{
	if (rc_sacked <= ctf_outstanding(tp))
		return(ctf_outstanding(tp) - rc_sacked);
	else {
		return (0);
	}
}

/* dummy function */
uint8_t
InLossRecovery()
{
    return true;
}