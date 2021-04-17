#ifndef _STMMAC_XSK_H_
#define _STMMAC_XSK_H_

#include <net/xdp_sock.h>
#include <net/xdp.h>
#include <linux/bpf_trace.h>
#include "stmmac.h"

//#define DEBUG_XSK
#ifdef DEBUG_XSK
#define DBG(...)	pr_info(__VA_ARGS__)
#else
#define DBG(...)
#endif

#define STMMAC_XDP_PASS		0
#define STMMAC_XDP_CONSUMED	BIT(0)
#define STMMAC_XDP_TX		BIT(1)
#define STMMAC_XDP_REDIR	BIT(2)

#define STMMAC_TX_SOURCE_RESET	0
#define STMMAC_TX_SOURCE_SKB	0
#define STMMAC_TX_SOURCE_UMEM	1
#define STMMAC_TX_SOURCE_FRAME	2

#define STMMAC_TX_XMIT_SAFE_AMOUNT		40

/* Refill timer restart time */
#define STMMAC_REFILL_GREEDILY_MS		1
#define STMMAC_REFILL_MS			500
#define STMMAC_INITIAL_REFILL_MS		500

#define STMMAC_REFILL_GREEDILY_THRESHOLD	10

#define DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

/* NDO prototypes */
int stmmac_bpf(struct net_device *dev, struct netdev_bpf *bpf);
int stmmac_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdp, u32 flags);
int stmmac_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags);

/* Inner usage */
inline u32 stmmac_tx_avail(struct stmmac_priv *priv, u32 queue);
int stmmac_rx_xsk(struct stmmac_priv *priv, int limit, u32 queue);
int stmmac_xdp_transmit_zc(struct stmmac_priv *priv, int napi_budget);
int stmmac_init_dma_engine_xsk(struct stmmac_priv *priv);

#endif
