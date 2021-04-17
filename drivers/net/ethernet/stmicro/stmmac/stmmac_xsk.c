#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/pinctrl/consumer.h>
#include <net/xdp_sock.h>
#include <net/xdp.h>
#include "stmmac_xsk.h"

/**
 * stmmac_zca_free() - this function reuse UMEM memory again with dirty desc.
 * @alloc: copy allocator structure
 * @handle: pointer of UMEM memory
 *
 * Called in case of using ZERO copy memory model when convert to XDP frame.
 */
static void stmmac_zca_free(struct zero_copy_allocator *alloc, unsigned long handle)
{
	int queue = 0;
	struct stmmac_priv *priv = container_of(alloc, struct stmmac_priv, zca);
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	unsigned int entry = rx_q->dirty_rx;
	struct stmmac_xsk_desc_map *buf = &rx_q->desc_map[entry];
	struct xdp_umem *umem = priv->xsk_umems[queue];
	struct dma_desc *p;
	u64 hr, mask;
	size_t len;

	DBG("%s-->\n", __FUNCTION__);

	if (priv->extend_desc)
		p = (struct dma_desc *)(rx_q->dma_erx + entry);
	else
		p = rx_q->dma_rx + entry;

	hr = umem->headroom + XDP_PACKET_HEADROOM;
	mask = umem->chunk_mask;
	handle &= mask;

	buf->dma_addr = xdp_umem_get_dma(umem, handle);
	buf->dma_addr += hr;
	buf->cpu_addr = xdp_umem_get_data(umem, handle);
	buf->cpu_addr += hr;
	buf->handle = xsk_umem_adjust_offset(umem, (u64)handle, umem->headroom);

	len = priv->xsk_umems[queue]->chunk_size_nohr - XDP_PACKET_HEADROOM;
	dma_sync_single_range_for_device(priv->device, buf->dma_addr,
					 buf->page_offset, len,
					 DMA_BIDIRECTIONAL);
	if (dma_mapping_error(priv->device, buf->dma_addr)) {
		netdev_err(priv->dev, "Rx DMA map failed\n");
		return;
	}

	stmmac_set_desc_addr(priv, p, buf->dma_addr);
	stmmac_refill_desc3(priv, rx_q, p);

	dma_wmb();
	stmmac_set_rx_owner(priv, p, priv->use_riwt);
	dma_wmb();

	rx_q->dirty_rx = STMMAC_GET_ENTRY(entry, DMA_RX_SIZE);

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_alloc_frames_for_xsk() - allocate memory for XSK frames
 * @priv: gemac main structure
 * @queue: queue number of the net device
 *
 * Any XSK packet form UMEM can be converted to frame for use in stack or
 * retransmit so allocate memory in advance.
 */
static int stmmac_alloc_frames_for_xsk(struct stmmac_priv *priv, int queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	size_t len = priv->xsk_umems[queue]->chunk_size_nohr - XDP_PACKET_HEADROOM;
	int err_i;
	int i;

	DBG("%s-->\n", __FUNCTION__);

	for (i = 0; i < DMA_TX_SIZE; i++) {
		tx_q->tx_skbuff_dma[i].page = kcalloc(1, len, GFP_KERNEL);
		if (!tx_q->tx_skbuff_dma[i].page)
			goto err;
	}

	DBG("%s<--\n", __FUNCTION__);

	return 0;

err:
	pr_err("AF_XDP: can not allocate memory for XSK frames\n");

	err_i = i;
	for (i = 0; i < err_i; ++i)
		kfree(tx_q->tx_skbuff_dma[i].page);

	return -ENOMEM;
}

/**
 * stmmac_free_frames_for_xsk() - free memory for XSK frames
 * @priv: gemac main structure
 * @queue: queue number of the net device
 */
static void stmmac_free_frames_for_xsk(struct stmmac_priv *priv, int queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	int i;

	DBG("%s-->\n", __FUNCTION__);

	for (i = 0; i < DMA_TX_SIZE; i++)
		kfree(tx_q->tx_skbuff_dma[i].page);

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_xmit_xdp_frame() - transmit one XSK frame
 * @priv: gemac main structure
 * @xdpf: pointer to the frame for transmitting
 *
 * Return: STMMAC_XDP_TX - ok	STMMAC_XDP_CONSUMED - failed
 */
static int stmmac_xmit_xdp_frame(struct stmmac_priv *priv, struct xdp_frame *xdpf)
{
	int queue = 0;
	struct dma_desc *desc;
	struct stmmac_tx_queue *tx_q = priv->tx_queue + queue;
	int entry = tx_q->cur_tx;
	dma_addr_t dma;
	u32 tx_avail = stmmac_tx_avail(priv, queue);

	if (atomic_read(&priv->tx_lock))
		return STMMAC_XDP_CONSUMED;
	else
		atomic_set(&priv->tx_lock, 1);

	if (!tx_avail)
		goto err;

	if (priv->extend_desc)
		desc = (struct dma_desc *)(tx_q->dma_etx + entry);
	else
		desc = tx_q->dma_tx + entry;

	memcpy(tx_q->tx_skbuff_dma[entry].page, xdpf->data, xdpf->len);
	dma = dma_map_single(priv->device, tx_q->tx_skbuff_dma[entry].page,
			     xdpf->len, DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, dma))
		goto err;

	tx_q->cur_tx = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	tx_q->tx_skbuff_dma[entry].tx_source_type = STMMAC_TX_SOURCE_FRAME;
	tx_q->tx_skbuff_dma[entry].buf = dma;
	tx_q->tx_skbuff_dma[entry].len = xdpf->len;
	tx_q->tx_skbuff_dma[entry].page_addr = xdpf->data;

	stmmac_set_desc_addr(priv, desc, dma);
	dma_wmb();
	stmmac_prepare_tx_desc(priv, desc, 1, xdpf->len, 0, priv->mode, 1, 1,
			       xdpf->len);
	dma_wmb();
	stmmac_enable_dma_transmission(priv, priv->ioaddr);

	__free_page(virt_to_page(xdpf->data));

	atomic_set(&priv->tx_lock, 0);

	return STMMAC_XDP_TX;

err:
	__free_page(virt_to_page(xdpf->data));

	atomic_set(&priv->tx_lock, 0);

	return STMMAC_XDP_CONSUMED;
}

/**
 * stmmac_xdp_transmit_zc() - transmit UMEM packets
 * @priv: gemac main structure
 * @napi_budget: budget to retransmit
 *
 * Main transmitting routine. Transmit packets got only from UMEM.
 *
 * Return: number of packets has been transmitted.
 */
int stmmac_xdp_transmit_zc(struct stmmac_priv *priv, int napi_budget)
{
	struct dma_desc *desc;
	struct xdp_desc xdp_desc;
	struct stmmac_tx_queue *tx_q = priv->tx_queue;
	struct stmmac_tx_info *tx_info;
	int entry = tx_q->cur_tx;
	int csum_insertion = 0;
	int queue = 0;
	u32 desc_processed = 0;
	u32 desc_avaliable = stmmac_tx_avail(priv, queue);
	dma_addr_t dma;

	/* Save batch of descriptors to ndo_xmit() */
	if (desc_avaliable < STMMAC_TX_XMIT_SAFE_AMOUNT)
		return 0;

	while ((napi_budget-- > 0) && (desc_avaliable-- > 0)) {
		/* Acquire data from UMEM */
		if (!xsk_umem_consume_tx(priv->xsk_umems[queue], &xdp_desc))
			break;

		/* Get descriptor by index */
		if (likely(priv->extend_desc))
			desc = (struct dma_desc *)(tx_q->dma_etx + entry);
		else
			desc = tx_q->dma_tx + entry;

		/* We use source type when clear Tx descriptors */
		tx_info = tx_q->tx_skbuff_dma + entry;
		tx_info->tx_source_type = STMMAC_TX_SOURCE_UMEM;

		/* Prepare for use with GEMAC */
		dma = xdp_umem_get_dma(priv->xsk_umems[queue], xdp_desc.addr);
		dma_sync_single_for_device(priv->device, dma, xdp_desc.len,
					   DMA_BIDIRECTIONAL);

		/* Fill in descriptors with data */
		stmmac_set_desc_addr(priv, desc, dma);
		stmmac_prepare_tx_desc(priv, desc, 1, xdp_desc.len,
				       csum_insertion, priv->mode, 1, 1,
				       xdp_desc.len);

		entry = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);
		tx_q->cur_tx = entry;

		++desc_processed;
	}

	/* Notify socket in user space about written data */
	if (desc_processed)
		xsk_umem_consume_tx_done(priv->xsk_umems[queue]);

	stmmac_enable_dma_transmission(priv, priv->ioaddr);

	return desc_processed;
}

/**
 * stmmac_rx_refill_xsk() - try to acquire descriptors for XSK from UMEM
 * @priv: gemac main structure
 * @queue: queue number of the net device
 *
 * Return: number of descriptors acquired form UMEM
 */
static int stmmac_rx_refill_xsk(struct stmmac_priv *priv, u32 queue)
{
	static atomic_t lock = ATOMIC_INIT(0);
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct stmmac_xsk_desc_map *buf;
	struct xdp_umem *umem = NULL;
	unsigned int entry = rx_q->dirty_rx;
	unsigned long timeout = jiffies + msecs_to_jiffies(1);
	int dirty = stmmac_rx_dirty(priv, queue);
	int cleaned = 0;
	u64 hr = 0;
	u64 handle;

	DBG("%s-->\n", __FUNCTION__);

	if ((priv->xsk_umems == NULL) || (priv->xsk_umems[queue] == NULL))
		return -EPERM;

	umem = priv->xsk_umems[queue];
	hr = umem->headroom + XDP_PACKET_HEADROOM;

	if (atomic_read(&lock))
		return -EBUSY;

	atomic_set(&lock, 1);

	while (dirty-- > 0) {
		struct dma_desc *p;
		size_t buf_size;

		/* Buffer info (extra data) is used for XSK */
		buf = &rx_q->desc_map[entry];

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		/* Acquire UMEM handle */
		if (!xsk_umem_peek_addr(priv->xsk_umems[queue], &handle)) {
			if (rx_q->rx_empty) {
				/* Notify user space to clear RX queue and refill FQ queue */
				if (xsk_umem_uses_need_wakeup(priv->xsk_umems[queue]))
					xsk_set_rx_need_wakeup(priv->xsk_umems[queue]);

				/* Try to acquire descriptors greedily */
				if (time_after(jiffies, timeout))
					break;
				else
					continue;
			}

			break;
		}
		dirty--;

		buf->dma_addr = xdp_umem_get_dma(umem, handle);
		buf->dma_addr += hr;
		buf->cpu_addr = xdp_umem_get_data(umem, handle);
		buf->cpu_addr += hr;
		buf->handle = handle + umem->headroom;

		/* Notify UMEM that we have taken one element */
		xsk_umem_discard_addr(priv->xsk_umems[queue]);

		rx_q->rx_empty = false;

		buf_size = priv->xsk_umems[queue]->chunk_size_nohr - XDP_PACKET_HEADROOM;
		dma_sync_single_range_for_device(priv->device, buf->dma_addr,
						 buf->page_offset, buf_size,
						 DMA_BIDIRECTIONAL);

		if (dma_mapping_error(priv->device, buf->dma_addr)) {
			netdev_err(priv->dev, "Rx DMA map failed\n");
			break;
		}

		stmmac_set_desc_addr(priv, p, buf->dma_addr);
		stmmac_refill_desc3(priv, rx_q, p);

		if (rx_q->rx_zeroc_thresh > 0)
			rx_q->rx_zeroc_thresh--;

		dma_wmb();
		stmmac_set_rx_owner(priv, p, priv->use_riwt);
		dma_wmb();
		entry = STMMAC_GET_ENTRY(entry, DMA_RX_SIZE);

		++cleaned;
	}
	rx_q->dirty_rx = entry;

	atomic_set(&lock, 0);

	DBG("%s<--\n", __FUNCTION__);

	return cleaned;
}

/**
 * stmmac_initial_refill() - try to acquire descriptors for XSK from UMEM in
 * initialization process.
 * @priv: gemac main structure
 * @queue: queue number of the net device
 *
 * Return: number of descriptors acquired form UMEM
 */
static int stmmac_initial_refill(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct dma_desc *p;
	struct xdp_umem *umem;
	int result = 0;
	int count = 0;
	int i;
	u64 hr;
	u64 handle;
	size_t len;

	/* Check if UMEM is initialized */
	if (priv->num_xsk_umems_used == 0)
		return result;

	umem = priv->xsk_umems[queue];
	hr = umem->headroom + XDP_PACKET_HEADROOM;

	for (i = 0; i < DMA_RX_SIZE; ++i) {
		/* Get descriptor pointer */
		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + i);
		else
			p = rx_q->dma_rx + i;

		/* Peek UMEM element */
		if (!xsk_umem_peek_addr(priv->xsk_umems[queue], &handle))
			break;

		/* Place UMEM element to store */
		rx_q->desc_map[i].dma_addr = xdp_umem_get_dma(umem, handle);
		rx_q->desc_map[i].dma_addr += hr;
		rx_q->desc_map[i].cpu_addr = xdp_umem_get_data(umem, handle);
		rx_q->desc_map[i].cpu_addr += hr;
		rx_q->desc_map[i].handle = handle + umem->headroom;

		/* Notify UMEM that we take one element */
		xsk_umem_discard_addr(priv->xsk_umems[queue]);

		/* Sync DMA for use on device */
		len = priv->xsk_umems[queue]->chunk_size_nohr - XDP_PACKET_HEADROOM;
		dma_sync_single_range_for_device(priv->device,
						 rx_q->desc_map[i].dma_addr,
						 rx_q->desc_map[i].page_offset,
						 len,
						 DMA_BIDIRECTIONAL);

		if (dma_mapping_error(priv->device, rx_q->desc_map[i].dma_addr)) {
			netdev_err(priv->dev, "Rx DMA map failed\n");
			break;
		}

		/* Setup DMA descriptor with new value */
		stmmac_set_desc_addr(priv, p, rx_q->desc_map[i].dma_addr);
		stmmac_refill_desc3(priv, rx_q, p);

		dma_wmb();
		stmmac_set_rx_owner(priv, p, priv->use_riwt);
		dma_wmb();

		++count;
	}

	if (count)
		rx_q->rx_empty = false;

	/* Setup ring descriptor pointers */
	rx_q->cur_rx = 0;
	rx_q->dirty_rx = count % DMA_RX_SIZE;

	DBG("Ring pointers [cur:dirty] = [%u:%u]\n", rx_q->cur_rx, rx_q->dirty_rx);

	/* This is unusual case just notify about it */
	if (count < DMA_RX_SIZE)
		pr_info("AF_XDP: Rx DMA ring is not filled completely %u of %u\n",
			count, DMA_RX_SIZE);

	return count;
}

/**
 * stmmac_refill_timer() - timer routine for deferred refilling
 * @t: associated timer
 */
static void stmmac_refill_timer(struct timer_list *t)
{
	struct stmmac_rx_queue *rx_q = from_timer(rx_q, t, rx_refill_timer);
	struct stmmac_priv *priv = rx_q->priv_data;

	stmmac_rx_refill_xsk(priv, rx_q->queue_index);

	/* Timer can be adjusted to different time in rx_poll_xsk() if necessary */
	mod_timer(&rx_q->rx_refill_timer,
		  jiffies + msecs_to_jiffies(STMMAC_REFILL_MS));
}

/**
 * stmmac_rx_timer() - timer routine to service Rx initialization and refilling
 * @t: associated timer
 *
 * It can happens there are no UMEMs descriptors when initialization finish so
 * run timer to try acquire UMEMs descriptors and finish ring initialization.
 */
static void stmmac_rx_timer(struct timer_list *t)
{
        struct stmmac_rx_queue *rx_q = from_timer(rx_q, t, rx_init_timer);
        struct stmmac_priv *priv = rx_q->priv_data;
        int is_refilled = 0;

        DBG("%s-->\n", __FUNCTION__);

        /* Try to do initial refill till it has succeed */
        is_refilled = stmmac_initial_refill(priv, rx_q->queue_index);
        if (!is_refilled) {
        	mod_timer(&rx_q->rx_init_timer,
        		  jiffies + msecs_to_jiffies(STMMAC_INITIAL_REFILL_MS));
            return;
        }

        /* It helps to solve problem with first descriptor owing */
        init_dma_rx_desc_rings_xsk(priv->dev);

        pr_info("AF_XDP: started\n");
    	stmmac_mac_set(priv, priv->ioaddr, true);
    	stmmac_start_all_dma(priv);

    	timer_setup(&rx_q->rx_refill_timer, stmmac_refill_timer, 0);
    	mod_timer(&rx_q->rx_refill_timer,
    		  jiffies + msecs_to_jiffies(STMMAC_REFILL_MS));

    	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_init_rx_service_timer() - service timer initialization routine
 * @priv: gemac main structure
 */
static void stmmac_init_rx_service_timer(struct stmmac_priv *priv)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[0];

	timer_setup(&rx_q->rx_init_timer, stmmac_rx_timer, 0);
	mod_timer(&rx_q->rx_init_timer, jiffies + msecs_to_jiffies(500));
}

/**
 * stmmac_run_xdp_zc() - run XDP filter and make actions based on the result
 * @priv: gemac main structure
 * @xdp: buffer to make action on it
 *
 * Return: code of action made on xdp buffer
 */
static int stmmac_run_xdp_zc(struct stmmac_priv *priv, struct xdp_buff *xdp)
{
	struct bpf_prog *xdp_prog;
	struct xdp_frame *xdpf;
	int result = STMMAC_XDP_PASS;
	int err;
	u64 offset;
	u32 act;

	rcu_read_lock();

	xdp_prog = READ_ONCE(priv->xdp_prog);
	if (xdp_prog) {
		act = bpf_prog_run_xdp(xdp_prog, xdp);
	} else {
		rcu_read_unlock();
		return -1;
	}

	offset = xdp->data - xdp->data_hard_start;
	xdp->handle = xsk_umem_adjust_offset(priv->xsk_umems[0], xdp->handle,
					     offset);

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		xdpf = convert_to_xdp_frame(xdp);
		if (unlikely(!xdpf)) {
			result = STMMAC_XDP_CONSUMED;
			break;
		}
		result = stmmac_xmit_xdp_frame(priv, xdpf);
		break;
	case XDP_REDIRECT:
		err = xdp_do_redirect(priv->dev, xdp, xdp_prog);
		result = !err ? STMMAC_XDP_REDIR : STMMAC_XDP_CONSUMED;
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
	case XDP_ABORTED:
		trace_xdp_exception(priv->dev, xdp_prog, act);
	case XDP_DROP:
		result = STMMAC_XDP_CONSUMED;
		break;
	}

	rcu_read_unlock();

	return result;
}

/**
 * stmmac_rx_xsk() - main receiving packets routine. Rx NAPI registered routine.
 * @priv: gemac main structure
 * @limit: NAPI budget
 * @queue: queue number of the net device
 *
 * This function can block (set flag) receive queue so there is no any receiving
 * operation until queue will be refilled
 *
 * Return: numbers of received packets
 */
int stmmac_rx_xsk(struct stmmac_priv *priv, int limit, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct stmmac_channel *ch = &priv->channel[queue];
	struct sk_buff *skb = NULL;
	struct xdp_buff xdp;
	unsigned int next_entry = rx_q->cur_rx;
	unsigned int count = 0;
	unsigned int error = 0;
	unsigned int len = 0;
	unsigned int xdp_res;
	int status = 0;
	int coe = priv->hw->rx_csum;
	bool do_flush = false;

	if (netif_msg_rx_status(priv)) {
		void *rx_head;

		netdev_dbg(priv->dev, "%s: descriptor ring:\n", __func__);
		if (priv->extend_desc)
			rx_head = (void *)rx_q->dma_erx;
		else
			rx_head = (void *)rx_q->dma_rx;

		stmmac_display_ring(priv, rx_head, DMA_RX_SIZE, true);
	}

	while ((count < limit) && !rx_q->rx_empty) {
		struct stmmac_xsk_desc_map *buf;
		struct dma_desc *np, *p;
		unsigned int sec_len;
		unsigned int hlen = 0, prev_len = 0;
		enum pkt_hash_types hash_type;
		int entry;
		u32 hash;

		if (!count && rx_q->state_saved) {
			skb = rx_q->state.skb;
			error = rx_q->state.error;
			len = rx_q->state.len;
		} else {
			rx_q->state_saved = false;
			skb = NULL;
			error = 0;
			len = 0;
		}

		if (count >= limit)
			break;

read_again:
		sec_len = 0;
		entry = next_entry;
		buf = rx_q->desc_map + entry;

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		status = stmmac_rx_status(priv, &priv->dev->stats,
					  &priv->xstats, p);

		/* Check if descriptor is ready to use */
		if (unlikely(status & dma_own))
			break;

		if (STMMAC_GET_ENTRY(rx_q->cur_rx, DMA_RX_SIZE) == rx_q->dirty_rx) {
			/* There are no more owned and refilled descriptors.
			 * All descriptors are read and queue is empty. Notify upper level.
			 */
			rx_q->rx_empty = true;
		} else {
			rx_q->cur_rx = STMMAC_GET_ENTRY(rx_q->cur_rx, DMA_RX_SIZE);
			next_entry = rx_q->cur_rx;
		}

		if (priv->extend_desc)
			np = (struct dma_desc *)(rx_q->dma_erx + next_entry);
		else
			np = rx_q->dma_rx + next_entry;

		prefetch(np);
		prefetch(buf->cpu_addr);

		if (priv->extend_desc)
			stmmac_rx_extended_status(priv, &priv->dev->stats,
						  &priv->xstats,
						  rx_q->dma_erx + entry);

		if (unlikely(status == discard_frame)) {
			error = 1;
			if (!priv->hwts_rx_en)
				priv->dev->stats.rx_errors++;
		}

		if (unlikely(error && (status & rx_not_ls)))
			goto read_again;

		if (unlikely(error)) {
			dev_kfree_skb(skb);
			count++;
			continue;
		}

		/* Buffer is good. Go on. */

		if (likely(status & rx_not_ls)) {
			len += priv->dma_buf_sz;
		} else {
			prev_len = len;
			len = stmmac_get_rx_frame_len(priv, p, coe);

			/* ACS is set; GMAC core strips PAD/FCS for IEEE 802.3
			 * Type frames (LLC/LLC-SNAP)
			 *
			 * llc_snap is never checked in GMAC >= 4, so this ACS
			 * feature is always disabled and packets need to be
			 * stripped manually.
			 */
			if (unlikely(priv->synopsys_id >= DWMAC_CORE_4_00) ||
				unlikely(status != llc_snap)) {
					len -= ETH_FCS_LEN;
			}
		}

		/* Sanity check */
		if (!len)
			continue;

		/* It's time to run XDP program */
		dma_sync_single_range_for_cpu(priv->device, buf->dma_addr,
					      buf->page_offset, len,
					      DMA_BIDIRECTIONAL);

		xdp.rxq = &rx_q->xdp_rxq;
		xdp.data = buf->cpu_addr;
		xdp.data_meta = xdp.data;
		xdp.data_hard_start = xdp.data - XDP_PACKET_HEADROOM;
		xdp.data_end = xdp.data + len;
		xdp.handle = buf->handle;

		xdp_res = stmmac_run_xdp_zc(priv, &xdp);
		if ((xdp_res == STMMAC_XDP_REDIR)) {
			count++;
			do_flush = true;
			continue;
		} else if ((xdp_res == STMMAC_XDP_TX) || (xdp_res == STMMAC_XDP_CONSUMED)) {
			count++;
			continue;
		}
		/* Pass XDP packet forward to the network stack */

		/* Allocate SKB if necessary */
		if (!skb) {
			int ret = stmmac_get_rx_header_len(priv, p, &hlen);

			if (priv->sph && !ret && (hlen > 0)) {
				sec_len = len;
				if (!(status & rx_not_ls))
					sec_len = sec_len - hlen;
				len = hlen;

				priv->xstats.rx_split_hdr_pkt_n++;
			}

			skb = napi_alloc_skb(&ch->rx_napi, len);
			if (!skb) {
				priv->dev->stats.rx_dropped++;
				count++;
				continue;
			}

			dma_sync_single_range_for_cpu(priv->device,
						      buf->dma_addr,
						      buf->page_offset, len,
						      DMA_BIDIRECTIONAL);

			skb_copy_to_linear_data(skb, buf->cpu_addr, len);
			skb_put(skb, len);
		} else {
			unsigned int buf_len = len - prev_len;

			if (likely(status & rx_not_ls))
				buf_len = priv->dma_buf_sz;

			dma_sync_single_range_for_cpu(priv->device,
						      buf->dma_addr,
						      buf->page_offset, len,
						      DMA_BIDIRECTIONAL);

			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->cpu_addr, 0, buf_len,
					priv->dma_buf_sz);
		}

		if (likely(status & rx_not_ls))
			goto read_again;

		/* Got entire packet into SKB. Finish it. */
		skb->protocol = eth_type_trans(skb, priv->dev);

		if (unlikely(!coe))
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (!stmmac_get_rx_hash(priv, p, &hash, &hash_type))
			skb_set_hash(skb, hash, hash_type);

		skb_record_rx_queue(skb, queue);
		napi_gro_receive(&ch->rx_napi, skb);

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += len;
		count++;
	}

	if (status & rx_not_ls) {
		rx_q->state_saved = true;
		rx_q->state.skb = skb;
		rx_q->state.error = error;
		rx_q->state.len = len;
	}

	if (do_flush)
		xdp_do_flush_map();

	stmmac_rx_refill_xsk(priv, queue);
	/* Make a decision whether we restart refilling and when */
	if (stmmac_rx_dirty(priv, queue) > STMMAC_REFILL_GREEDILY_THRESHOLD) {
		/* Notify user application we run out of descriptors */
		if (xsk_umem_uses_need_wakeup(priv->xsk_umems[queue]))
			xsk_set_rx_need_wakeup(priv->xsk_umems[queue]);

		/* Start looking for descriptors in hard way */
		mod_timer(&rx_q->rx_refill_timer,
			  jiffies + msecs_to_jiffies(STMMAC_REFILL_GREEDILY_MS));
	} else {
		/* We don't want to notify user application to start
		 * looking for descriptors in hard way so clear flag
		 */
		if (xsk_umem_uses_need_wakeup(priv->xsk_umems[queue]))
			xsk_clear_rx_need_wakeup(priv->xsk_umems[queue]);

		/* Just from time to time check if clearing go well */
		mod_timer(&rx_q->rx_refill_timer,
			  jiffies + msecs_to_jiffies(STMMAC_REFILL_MS));
	}

	priv->xstats.rx_pkt_n += count;

	/* Sanity check. If it happens notify user and let the NAPI works  */
	if (WARN_ONCE(count > limit, "NAPI return value higher than budget!\n"))
		count = limit;

	return count;
}

/**
 * stmmac_remove_xsk_umem() - free UMEM resources
 * @priv: gemac main structure
 * @qid: queue number of the net device
 */
static void stmmac_remove_xsk_umem(struct stmmac_priv *priv, u16 qid)
{
	DBG("%s-->\n", __FUNCTION__);

	priv->xsk_umems[qid] = NULL;
	priv->num_xsk_umems_used--;

	if (priv->num_xsk_umems == 0) {
		kfree(priv->xsk_umems);
		priv->xsk_umems = NULL;
		priv->num_xsk_umems = 0;
	}

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_alloc_xsk_umems() - alloc UMEM resources
 * @priv: gemac main structure
 *
 * Return: 0 - ok	-ENOMEM - fail
 */
static int stmmac_alloc_xsk_umems(struct stmmac_priv *priv)
{
	if (priv->xsk_umems)
		return 0;

	DBG("%s-->\n", __FUNCTION__);

	priv->num_xsk_umems_used = 0;
	priv->num_xsk_umems = MTL_MAX_RX_QUEUES;
	priv->xsk_umems = kcalloc(priv->num_xsk_umems, sizeof(*priv->xsk_umems),
				  GFP_KERNEL);
	if (!priv->xsk_umems) {
		priv->num_xsk_umems = 0;
		return -ENOMEM;
	}

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * stmmac_add_xsk_umem() - add to UMEM auxiliary data
 * @priv: gemac main structure
 * @umem: allocated UMEM
 * @qid: queue number of the net device
 *
 * Return: 0 - ok	-ENOMEM - fail
 */
static int stmmac_add_xsk_umem(struct stmmac_priv *priv, struct xdp_umem *umem,
			       u16 qid)
{
	int err;

	DBG("%s-->\n", __FUNCTION__);

	err = stmmac_alloc_xsk_umems(priv);
	if (err)
		return err;

	priv->xsk_umems[qid] = umem;
	priv->num_xsk_umems_used++;

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * stmmac_xsk_umem_dma_map() - map DMA memory for UMEM descriptors
 * @priv: gemac main structure
 * @qid: associated UMEM
 *
 * Return: 0 - ok	-ENOMEM - fail
 */
static int stmmac_xsk_umem_dma_map(struct stmmac_priv *priv,
				   struct xdp_umem *umem)
{
	struct device *dev = priv->device;
	unsigned int i, j;
	dma_addr_t dma;

	DBG("%s-->\n", __FUNCTION__);

	for (i = 0; i < umem->npgs; i++) {
		dma = dma_map_page_attrs(dev, umem->pgs[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, DMA_ATTR);
		if (dma_mapping_error(dev, dma))
			goto out_unmap;

		umem->pages[i].dma = dma;
	}

	DBG("%s<--\n", __FUNCTION__);

	return 0;

out_unmap:
	for (j = 0; j < i; j++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, DMA_ATTR);
		umem->pages[i].dma = 0;
	}

	return -ENOMEM;
}

/**
 * stmmac_xdp_setup() - setup new XDP filter
 * @dev: gemac main structure
 * @prog: filter program
 *
 * Return: 0 - ok
 */
static int stmmac_xdp_setup(struct net_device *dev, struct bpf_prog *prog)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct bpf_prog *old_prog;

	DBG("%s-->\n", __FUNCTION__);

	synchronize_rcu();
	old_prog = xchg(&priv->xdp_prog, prog);
	rcu_assign_pointer(priv->xdp_prog, prog);

	if (old_prog)
		bpf_prog_put(old_prog);

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * free_dma_rx_desc_resources_xsk() - free DMA descriptors for every ring
 * @priv: gemac main structure
 */
void free_dma_rx_desc_resources_xsk(struct stmmac_priv *priv)
{
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 queue;

	DBG("%s-->\n", __FUNCTION__);

	/* Free RX queue resources */
	for (queue = 0; queue < rx_count; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		/* Free DMA regions of consistent memory previously allocated */
		if (!priv->extend_desc)
			dma_free_coherent(priv->device,
					  DMA_RX_SIZE * sizeof(struct dma_desc),
					  rx_q->dma_rx, rx_q->dma_rx_phy);
		else
			dma_free_coherent(priv->device,
					  DMA_RX_SIZE * sizeof(struct dma_extended_desc),
					  rx_q->dma_erx, rx_q->dma_rx_phy);
	}

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * alloc_dma_rx_desc_resources_xsk() - allocate DMA descriptors for every ring
 * @priv: gemac main structure
 *
 * Return: 0 - ok	-ENOMEM - fail
 */
int alloc_dma_rx_desc_resources_xsk(struct stmmac_priv *priv)
{
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 err_queue;
	size_t len;

	DBG("%s-->\n", __FUNCTION__);

	/* RX queues buffers and DMA */
	for (queue = 0; queue < rx_count; ++queue) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		rx_q->queue_index = queue;
		rx_q->priv_data = priv;

		if (priv->extend_desc) {
			len = DMA_RX_SIZE * sizeof(struct dma_extended_desc);
			rx_q->dma_erx = dma_alloc_coherent(priv->device,
							   len,
							   &rx_q->dma_rx_phy,
							   GFP_KERNEL);
			if (!rx_q->dma_erx)
				goto err_dma;
		} else {
			len = DMA_RX_SIZE * sizeof(struct dma_desc);
			rx_q->dma_rx = dma_alloc_coherent(priv->device,
							  len,
							  &rx_q->dma_rx_phy,
							  GFP_KERNEL);
			if (!rx_q->dma_rx)
				goto err_dma;
		}
	}

	DBG("%s<--\n", __FUNCTION__);

	return 0;

err_dma:
	pr_err("AF_XDP: Can not allocate DMA coherent memory!\n");

	err_queue = queue;
	for (queue = 0; queue < err_queue; ++queue) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->extend_desc) {
			len = DMA_RX_SIZE * sizeof(struct dma_extended_desc);
			dma_free_coherent(priv->device,
					 len, rx_q->dma_erx, rx_q->dma_rx_phy);
		} else {
			len = DMA_RX_SIZE * sizeof(struct dma_desc);
			dma_free_coherent(priv->device,
					  len,  rx_q->dma_rx, rx_q->dma_rx_phy);
		}
	}

	return -ENOMEM;
}

/**
 * stmmac_txrx_ring_disable() - stop and free resources for ring. Stop DMA engine
 * @priv: gemac main structure
 * @ring: number of associated ring
 */
static void stmmac_txrx_ring_disable(struct stmmac_priv *priv, int ring)
{
	struct stmmac_channel *ch = &priv->channel[ring];
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);

	DBG("%s-->\n", __FUNCTION__);

	/* Sanity check */
	if (ring > maxq)
		return;

	/* Stop GEMAC engine */
	stmmac_mac_set(priv, priv->ioaddr, false);
	stmmac_stop_all_dma(priv);

	/* Wait finishing last transactions */
	msleep(100);

	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, ring));

	/* Everything is ready to stop NAPIs */
	if (ring < rx_queues_cnt)
		napi_disable(&ch->rx_napi);
	if (ring < tx_queues_cnt)
		napi_disable(&ch->tx_napi);

	if (priv->num_xsk_umems_used && priv->xsk_umems[ring]) {
		/* Disable UMEM resources */
		DBG("%s: UMEM memory model disable\n", __FUNCTION__);

		xdp_do_flush_map();
		xdp_rxq_info_unreg(&priv->rx_queue[ring].xdp_rxq);
		stmmac_free_frames_for_xsk(priv, ring);
		free_dma_rx_desc_resources_xsk(priv);
	} else {
		/* Disable resources in case of using pool of pages */
		DBG("%s: page pool memory model disable\n", __FUNCTION__);

		free_dma_rx_desc_resources(priv);
	}

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_reset_watchdog_envent() - defer transmit time to avoid network schedule
 * timeout.
 * @priv: gemac main structure
 * @ring: ring number
 *
 * Reset start time to allow acquire UMEM descriptors. It would be better to
 * disable ring till it own UMEM but now it's made that way.
 */
static void stmmac_reset_watchdog_envent(struct stmmac_priv *priv, int ring)
{
	struct netdev_queue *txq;

	txq = netdev_get_tx_queue(priv->dev, ring);
	txq->trans_start = jiffies + priv->dev->watchdog_timeo;
}

/**
 *
 * stmmac_txrx_ring_enable() - allocate resources and run ring. Start DMA engine
 * @priv: gemac main structure
 * @ring: number of associated ring
 *
 * Return: 0 - ok	-ENOMEM - failure
 */
static int stmmac_txrx_ring_enable(struct stmmac_priv *priv, int ring)
{
	struct stmmac_channel *ch = &priv->channel[ring];
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[ring];
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	bool enable_gemac = false;

	DBG("%s-->\n", __FUNCTION__);

	/* Sanity check */
	if (ring > maxq)
		return -EPERM;

	if (priv->num_xsk_umems_used && priv->xsk_umems[ring]) {
		/* Allocate UMEM resources */
		DBG("%s: UMEM memory model enable\n", __FUNCTION__);

		priv->zca.free = stmmac_zca_free;
		WARN_ON(xdp_rxq_info_reg_mem_model(&priv->rx_queue[ring].xdp_rxq,
						   MEM_TYPE_ZERO_COPY,
						   &priv->zca));

		if (alloc_dma_rx_desc_resources_xsk(priv))
			goto err;
		stmmac_alloc_frames_for_xsk(priv, ring);

		stmmac_init_rx_service_timer(priv);
	} else {
		/* Allocate resources in case of using pool of pages */
		DBG("%s: page pool memory model enable\n", __FUNCTION__);

		if (alloc_dma_rx_desc_resources(priv))
			goto err;
		init_dma_desc_rings(priv->dev, GFP_KERNEL);

		/* Do restrict setup instead of full because driver isn't ready to run */
		stmmac_hw_restrict_setup(priv->dev, true);

		enable_gemac = true;
	}

	stmmac_reset_watchdog_envent(priv, ring);
	netif_tx_start_queue(netdev_get_tx_queue(priv->dev, ring));

	stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    rx_q->dma_rx_phy, ring);

	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			     (DMA_RX_SIZE * sizeof(struct dma_desc));
	stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
			       rx_q->rx_tail_addr, ring);

	/* In case of UMEM this variables will be overridden in initial refill */
	rx_q->cur_rx = 0;
	rx_q->dirty_rx = 0;

	/* Ready to start NAPIs */
	if (ring < rx_queues_cnt)
		napi_enable(&ch->rx_napi);
	if (ring < tx_queues_cnt)
		napi_enable(&ch->tx_napi);

	/* Enable GEMAC engine here in case of using page pool */
	if (enable_gemac) {
		stmmac_mac_set(priv, priv->ioaddr, true);
		stmmac_start_all_dma(priv);
	}

	DBG("%s<--\n", __FUNCTION__);

	return 0;

err:
	pr_err("AF_XDP: can not enable ring %d\n", ring);
	return -ENOMEM;
}

/**
 * stmmac_umem_enable() - allocate resources and enable UMEM
 * @priv: gemac main structure
 * @umem: pointer to socket UMEM representation
 * @qid: number of the queue to associate with
 *
 * Return: 0 - ok	< 0 - fail
 */
static int stmmac_umem_enable(struct stmmac_priv *priv, struct xdp_umem *umem,
			      u16 qid)
{
	struct xdp_umem_fq_reuse *reuseq;
	int err = -1;
	bool if_running;

	DBG("%s-->\n", __FUNCTION__);


	if (qid >= priv->plat->rx_queues_to_use)
		return -EINVAL;

	err = xdp_rxq_info_reg(&priv->rx_queue[0].xdp_rxq, priv->dev, 0);
	if (err)
		return err;

	reuseq = xsk_reuseq_prepare(DMA_RX_SIZE);
	if (!reuseq)
		return -ENOMEM;

	if_running = netif_running(priv->dev);
	if (if_running)
		stmmac_txrx_ring_disable(priv, qid);

	/* Setup UMEM and XDP auxiliary data */
	if (stmmac_add_xsk_umem(priv, umem, qid))
		return err;

	xsk_reuseq_free(xsk_reuseq_swap(umem, reuseq));

	err = stmmac_xsk_umem_dma_map(priv, umem);
	if (err)
		return err;

	priv->xsk_umems[qid] = umem;

	/* Enable rings */
	if (if_running)
		stmmac_txrx_ring_enable(priv, qid);

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * stmmac_xsk_umem_dma_unmap() - free UMEM DMA resources
 * @priv: gemac main structure
 * @umem: associated UMEM
 */
static void stmmac_xsk_umem_dma_unmap(struct stmmac_priv *priv,
				      struct xdp_umem *umem)
{
	struct device *dev = priv->device;
	unsigned int i;

	DBG("%s-->\n", __FUNCTION__);

	for (i = 0; i < umem->npgs; i++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, DMA_ATTR);
		umem->pages[i].dma = 0;
	}

	DBG("%s<--\n", __FUNCTION__);
}

/**
 * stmmac_umem_disable() - free resources and disable UMEM
 * @priv: gemac main structure
 * @qid: number of the queue to associate with
 *
 * Return: 0 - ok	< 0 - fail
 */
static int stmmac_umem_disable(struct stmmac_priv *priv, u16 qid)
{
	struct xdp_umem *umem;
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[qid];
	bool if_running;
	
	DBG("%s-->\n", __FUNCTION__);

	umem = xdp_get_umem_from_qid(priv->dev, qid);
	if (!umem)
		return -EINVAL;

	if_running = netif_running(priv->dev);

	if (if_running)
		stmmac_txrx_ring_disable(priv, qid);

	stmmac_xsk_umem_dma_unmap(priv, umem);
	stmmac_remove_xsk_umem(priv, qid);

	del_timer_sync(&rx_q->rx_init_timer);
	del_timer_sync(&rx_q->rx_refill_timer);

	if (if_running)
		stmmac_txrx_ring_enable(priv, qid);

	priv->xsk_umems = NULL;

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * stmmac_umem_setup() - wrapper for enable/disable UMEM
 * @priv: gemac main structure
 * @umem: pointer to socket UMEM representation
 * @qid: number of the associated queue
 *
 * Return: 0 - ok	< 0 - fail
 */
static int stmmac_umem_setup(struct stmmac_priv *priv, struct xdp_umem *umem,
			     u16 qid)
{
	return umem ? stmmac_umem_enable(priv, umem, qid) : \
		      stmmac_umem_disable(priv, qid);
}

/**
 * stmmac_bpf() - callback of network stack for setup bpf or enable/disable UMEM
 * @priv: gemac main structure
 * @bpf: network stack representation of bpf
 *
 * Return: 0 - ok	< 0 - fail
 */
int stmmac_bpf(struct net_device *dev, struct netdev_bpf *bpf)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	DBG("%s-->\n", __FUNCTION__);

	switch (bpf->command) {
		case XDP_SETUP_PROG:
			if (!priv->xsk_umems) {
				pr_err("AF_XDP: Copy mode is not supported\n");
				return -EPERM;
			}
			return stmmac_xdp_setup(dev, bpf->prog);
		case XDP_QUERY_PROG:
			bpf->prog_id = priv->xdp_prog ? priv->xdp_prog->aux->id : 0;
			return 0;
		case XDP_SETUP_XSK_UMEM:
			return stmmac_umem_setup(priv, bpf->xsk.umem,
						 bpf->xsk.queue_id);
		default:
			return -EINVAL;
	}

	DBG("%s<--\n", __FUNCTION__);

	return -EPERM;
}

/**
 * stmmac_xdp_xmit() - do redirect to non mapped place as external network
 * @dev: network device to transmit
 * @n: number of XDP frames
 * @xdp: pointer to xdp frames array
 * @flags: extra flags from network stack
 *
 * Return: number of redirected frames
 */
int stmmac_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdp,
		    u32 flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int drops = 0;
	int result;
	int i;

	DBG("%s-->\n", __FUNCTION__);

	for (i = 0; i < n; ++i) {
		result = stmmac_xmit_xdp_frame(priv, xdp[i]);
		if (result != STMMAC_XDP_TX) {
			xdp_return_frame_rx_napi(xdp[i]);
			drops++;
		}
	}

	DBG("%s<--\n", __FUNCTION__);

	return n - drops;
}

/**
 * stmmac_xsk_wakeup() - Wake up Rx or/and Tx queue
 * @dev: associated network device
 * @queue_id: number of the queue
 * @flags: sent action
 *
 * User space application or network stack can wake up driver in case of absence
 * resource.
 *
 * Return: 0 - ok
 */
int stmmac_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	DBG("%s-->\n", __FUNCTION__);

	/* Wake up request can be sent from poll of socket */

	if (flags & XDP_WAKEUP_TX)
		/* Run NAPI tx engine to kick transfer or clean descriptors */
		if (likely(napi_schedule_prep(&priv->channel[0].tx_napi))) {
			__napi_schedule(&priv->channel[queue_id].tx_napi);
			//xsk_clear_tx_need_wakeup(priv->xsk_umems[queue_id]);
		}

	if (flags & XDP_WAKEUP_RX)
		/* Run NAPI rx engine to start receiving or clean descriptors */
		if (likely(napi_schedule_prep(&priv->channel[0].rx_napi))) {
			__napi_schedule(&priv->channel[queue_id].rx_napi);
			//xsk_clear_rx_need_wakeup(priv->xsk_umems[queue_id]);
		}

	DBG("%s<--\n", __FUNCTION__);

	return 0;
}

/**
 * stmmac_init_dma_engine_xsk() - initialize DMA engine in case of using XSK
 * @priv: gemac main structure
 */
int stmmac_init_dma_engine_xsk(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	struct stmmac_rx_queue *rx_q;
	struct stmmac_tx_queue *tx_q;
	u32 chan = 0;
	int atds = 0;
	int ret = 0;

	if (!priv->plat->dma_cfg || !priv->plat->dma_cfg->pbl) {
		dev_err(priv->device, "Invalid DMA configuration\n");
		return -EINVAL;
	}

	if (priv->extend_desc && (priv->mode == STMMAC_RING_MODE))
		atds = 1;

	/* DMA Configuration */
	stmmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg, atds);

	if (priv->plat->axi)
		stmmac_axi(priv, priv->ioaddr, priv->plat->axi);

	/* DMA CSR Channel configuration */
	for (chan = 0; chan < dma_csr_ch; chan++)
		stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
		rx_q = &priv->rx_queue[chan];

		stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    rx_q->dma_rx_phy, chan);

		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
				     (DMA_RX_SIZE * sizeof(struct dma_desc));
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       rx_q->rx_tail_addr, chan);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
		tx_q = &priv->tx_queue[chan];

		stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    tx_q->dma_tx_phy, chan);

		tx_q->tx_tail_addr = tx_q->dma_tx_phy;
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);
	}

	return ret;
}
