#include <stdint.h>

struct tcp_cb {
    uint32_t cwnd;
    uint32_t rwnd;
    uint32_t smss; /* Sender Maximum Segment Size (segmax) */
    uint32_t ssthresh; /* Slow Start Threshould */
    uint32_t snd_una; /* Sent but unacknowledged */
    uint32_t bytes_acked; /* Per RFC 3465 Section 2.1 */
    uint16_t nsegs;
    uint32_t snd_seq_max; /* Highest Sequence Sent */
	uint32_t SRTT;	/* smoothed round trip time << 3 in usecs */
};

static int initial_window(struct tcp_cb *cb);