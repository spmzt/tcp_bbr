#include <stdint.h>

#define	TCP_NSTATES	11

#define	TCPS_CLOSED		0	/* closed */
#define	TCPS_LISTEN		1	/* listening for connection */
#define	TCPS_SYN_SENT		2	/* active, have sent syn */
#define	TCPS_SYN_RECEIVED	3	/* have sent and received syn */
/* states < TCPS_ESTABLISHED are those where connections not established */
#define	TCPS_ESTABLISHED	4	/* established */
#define	TCPS_CLOSE_WAIT		5	/* rcvd fin, waiting for close */
/* states > TCPS_CLOSE_WAIT are those where user has closed */
#define	TCPS_FIN_WAIT_1		6	/* have closed, sent fin */
#define	TCPS_CLOSING		7	/* closed xchd FIN; await FIN ACK */
#define	TCPS_LAST_ACK		8	/* had fin and close; await FIN ACK */
/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
#define	TCPS_FIN_WAIT_2		9	/* have closed, fin is acked */
#define	TCPS_TIME_WAIT		10	/* in 2*msl quiet wait after close */

#define	TCPS_HAVERCVDSYN(s)	((s) >= TCPS_SYN_RECEIVED)
#define	TCPS_HAVEESTABLISHED(s)	((s) >= TCPS_ESTABLISHED)
#define	TCPS_HAVERCVDFIN(s)	\
    ((s) == TCPS_CLOSE_WAIT || ((s) >= TCPS_CLOSING && (s) != TCPS_FIN_WAIT_2))

/*
 *
 * Flags and utility macros for the t_flags field.
 */
#define	TF_ACKNOW	0x00000001	/* ack peer immediately */
#define	TF_DELACK	0x00000002	/* ack, but try to delay it */
#define	TF_NODELAY	0x00000004	/* don't delay packets to coalesce */
#define	TF_NOOPT	0x00000008	/* don't use tcp options */
#define	TF_SENTFIN	0x00000010	/* have sent FIN */
#define	TF_REQ_SCALE	0x00000020	/* have/will request window scaling */
#define	TF_RCVD_SCALE	0x00000040	/* other side has requested scaling */
#define	TF_REQ_TSTMP	0x00000080	/* have/will request timestamps */
#define	TF_RCVD_TSTMP	0x00000100	/* a timestamp was received in SYN */
#define	TF_SACK_PERMIT	0x00000200	/* other side said I could SACK */
#define	TF_NEEDSYN	0x00000400	/* send SYN (implicit state) */
#define	TF_NEEDFIN	0x00000800	/* send FIN (implicit state) */
#define	TF_NOPUSH	0x00001000	/* don't push */

/*
* saved values for bad rxmit valid
* Note: accessing and restoring from
* these may only be done in the 1st
* RTO recovery round (t_rxtshift == 1)
*/
#define	TF_PREVVALID	0x00002000

#define	TF_WAKESOR	0x00004000	/* wake up receive socket */
#define	TF_GPUTINPROG	0x00008000	/* Goodput measurement in progress */
#define	TF_MORETOCOME	0x00010000	/* More data to be appended to sock */
#define	TF_SONOTCONN	0x00020000	/* needs soisconnected() on ESTAB */
#define	TF_LASTIDLE	0x00040000	/* connection was previously idle */
#define	TF_RXWIN0SENT	0x00080000	/* sent a receiver win 0 in response */
#define	TF_FASTRECOVERY	0x00100000	/* in NewReno Fast Recovery */
#define	TF_WASFRECOVERY	0x00200000	/* was in NewReno Fast Recovery */
#define	TF_SIGNATURE	0x00400000	/* require MD5 digests (RFC2385) */
#define	TF_FORCEDATA	0x00800000	/* force out a byte */
#define	TF_TSO		0x01000000	/* TSO enabled on this connection */
#define	TF_TOE		0x02000000	/* this connection is offloaded */
#define	TF_CLOSED	0x04000000	/* close(2) called on socket */
#define TF_SENTSYN      0x08000000      /* At least one syn has been sent */
#define	TF_LRD		0x10000000	/* Lost Retransmission Detection */
#define	TF_CONGRECOVERY	0x20000000	/* congestion recovery mode */
#define	TF_WASCRECOVERY	0x40000000	/* was in congestion recovery */
#define	TF_FASTOPEN	0x80000000	/* TCP Fast Open indication */

#define	IN_FASTRECOVERY(t_flags)	(t_flags & TF_FASTRECOVERY)
#define	ENTER_FASTRECOVERY(t_flags)	t_flags |= TF_FASTRECOVERY
#define	EXIT_FASTRECOVERY(t_flags)	t_flags &= ~TF_FASTRECOVERY

#define	IN_CONGRECOVERY(t_flags)	(t_flags & TF_CONGRECOVERY)
#define	ENTER_CONGRECOVERY(t_flags)	t_flags |= TF_CONGRECOVERY
#define	EXIT_CONGRECOVERY(t_flags)	t_flags &= ~TF_CONGRECOVERY

#define	IN_RECOVERY(t_flags) (t_flags & (TF_CONGRECOVERY | TF_FASTRECOVERY))
#define	ENTER_RECOVERY(t_flags) t_flags |= (TF_CONGRECOVERY | TF_FASTRECOVERY)
#define	EXIT_RECOVERY(t_flags) t_flags &= ~(TF_CONGRECOVERY | TF_FASTRECOVERY)

#define	BYTES_THIS_ACK(tp, th)	(th->th_ack - tp->snd_una)


struct tcp_cb {
    uint32_t cwnd;
    uint32_t rwnd;
    uint32_t smss; /* Sender Maximum Segment Size (segmax) */
    uint32_t ssthresh; /* Slow Start Threshould */
    uint32_t snd_una; /* Sent but unacknowledged */
    uint32_t delivered; /* The total amount of data (tracked in octets or in packets) delivered so far over the lifetime of the transport connection C. */
    uint32_t bytes_acked; /* Per RFC 3465 Section 2.1 */
    uint32_t snd_max; /* Maximum sequence number that was ever transmitted */
	uint32_t SRTT;	/* smoothed round trip time << 3 in usecs */
    uint32_t flags; /* tcp flags */
    uint32_t pipe; /* The sender's estimate of the amount of data outstanding in the network (measured in octets or packets).
                    * This includes data packets in the current outstanding window that are being transmitted or retransmitted and have not been SACKed or marked lost (e.g. "pipe" from [RFC6675]).
                    * This does not include pure ACK packets. */
    uint16_t nsegs;
    uint8_t app_limited:1,
            state:4, /* tcp states */
            unused:3;
};

extern int initial_window(struct tcp_cb *cb);
extern uint8_t InLossRecovery();