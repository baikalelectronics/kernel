#ifndef SPI_DB_ESPI_H
#define SPI_DB_ESPI_H


#define ESPI_FIFO_LEN	256
#define ESPI_DUMMY_DATA	0xFF
#define ESPI_FREQUENCY	25000000


/* driver data */
struct espi_data {
	struct spi_master	*master;

	/* hardware */
	int			irq;
	void __iomem		*regs;
	struct clk		*clk;
	int			*cs_gpios;
	int			cnt_gpios;

	/* transfer */
	uint32_t		rxlen;
	uint32_t		txlen;
	uint8_t			*tx;
	uint8_t			*rx;
};


/* ---------- */
/* regs       */
/* ---------- */

#define ESPI_CR1	0x0	/* control 1 */
#define ESPI_CR2	0x4	/* control 2 */
#define ESPI_TX_FIFO	0xc 	/* fifo */
#define ESPI_TX_FBCAR	0x14	/* byte count */
#define ESPI_TX_FAETR	0x18	/* almost empty threshold */
#define ESPI_RX_FIFO	0x1c	/* fifo */
#define ESPI_RX_FBCAR	0x24	/* byte count */
#define ESPI_RX_FAFTR	0x28	/* almost full threshold */
#define ESPI_ISR	0x30	/* irq status */
#define ESPI_IMR	0x34	/* irq mask */
#define ESPI_IVR	0x38	/* irq vector */
#define ESPI_CR3	0x54	/* espi control */
#define ESPI_RBCR	0x58	/* espi response byte count */
#define ESPI_CIR1	0xe0	/* id1 */
#define ESPI_CIR2	0xe4	/* id2 */
#define ESPI_DCR	0x40	/* dma control */
#define ESPI_TDBAR	0x44	/* dma transmit base address */
#define ESPI_TDCAR	0x48	/* dma transmit current address */
#define ESPI_RDBAR	0x4c	/* dma receive base address */
#define ESPI_RDCAR	0x50	/* dma receive current address */


typedef union {
	uint32_t val;
	struct {
		//low
		uint32_t scr :1;	/* reset */
		uint32_t sce :1;	/* enable */
		uint32_t mss :1;	/* select - master/slave */
		uint32_t cph :1;	/* clock phase */
		uint32_t cpo :1;	/* clock polarity */
		//hi
	} bits;
} espi_cr1_t;
#define ESPI_CR1_SCR_RESET          0
#define ESPI_CR1_SCR_NORESET        1
#define ESPI_CR1_SCE_CORE_DISABLE   0
#define ESPI_CR1_SCE_CORE_ENABLE    1
#define ESPI_CR1_MSS_MODE_MASTER    0
#define ESPI_CR1_MSS_MODE_SLAVE     1
#define ESPI_CR1_CPH_CLKPHASE_ODD   0
#define ESPI_CR1_CPH_CLKPHASE_EVEN  1
#define ESPI_CR1_CPO_CLKPOLAR_LOW   0
#define ESPI_CR1_CPO_CLKPOLAR_HIGH  1

typedef union {
	uint32_t val;
	struct {
		//low
		uint32_t  sso :3;	/* slave select (ss0-ss7) */
		uint32_t  srd :1;	/* read disable */
		uint32_t  sri :1;	/* read first byte ignore */
		uint32_t  mlb :1;	/* least sign git first */
		uint32_t  mte :1;	/* master transfer enable */
		//hi
	} bits;
} espi_cr2_t;

#define ESPI_CR2_SRD_RX_DISABLE 0
#define ESPI_CR2_SRD_RX_ENABLE 1
#define ESPI_CR2_SRI_FIRST_RESIEV 0
#define ESPI_CR2_SRI_FIRST_IGNORE 1
#define ESPI_CR2_MLB_MSB 0
#define ESPI_CR2_MLB_LSB 1
#define ESPI_CR2_MTE_TX_DISABLE 0
#define ESPI_CR2_MTE_TX_ENABLE 1   /* self clear !! */

/* struct espi_tx_fifo_t  {} */
/* struct espi_rx_fifo_t  {} */
/* struct espi_tx_fbcar_t {} */
/* struct espi_rx_fbcar_t {} */
/* struct espi_tx_faetr_t {} */
/* struct espi_rx_faftr_t {} */

#define  ESPI_RX_FAFTR_DISABLE 0xFF
#define  ESPI_TX_FAETR_DISABLE 0x0
#define  ESPI_ISR_CLEAR        0xFF   /* clear by writing "1" */
#define  ESPI_IMR_DISABLE      0x0
#define  ESPI_IMR_ENABLE       0xFF

/* isr, imr, ivr */
typedef union {
	uint32_t val;
	struct {
		//lo
		uint32_t tx_underflow        :1;
		uint32_t tx_overrun          :1;
		uint32_t rx_underrun         :1;
		uint32_t rx_overrun          :1;
		uint32_t tx_almost_empty     :1;
		uint32_t rx_almost_full      :1;
		uint32_t tx_done_master      :1;
		uint32_t tx_done_slave       :1;
		uint32_t alert               :8;
		uint32_t rx_crc_error        :1;
		//hi
	} bits;
} espi_irq_t;
#define ESPI_IRQ_DISABLE 0
#define ESPI_IRQ_ENABLE  1

/* struct espi_dcr_t   {} */
/* struct espi_tdbar_t {} */
/* struct espi_tdcar_t {} */
/* struct espi_rdbar_t {} */
/* struct espi_rdcar_t {} */

typedef union {
	uint32_t val;
	struct {
		//low
		uint32_t  ese :1;	/* enable */
		uint32_t  rms :1;	/* reset# direction */
		uint32_t  raa :1;	/* reset# assertion active */
		uint32_t  cre :1;	/* crc receive enable */
		uint32_t  crf :1;	/* crc receive byte placed in fifo */
		uint32_t  wsr :1;	/* wait state response */
		uint32_t  sdl :2;	/* spi data lines (single, dual, quad) */
		//hi
	} bits;
} espi_cr3_t;

/* struct espi_rbcr_t {} */
/* struct espi_cir1_t {} */
/* struct espi_cir2_t {} */


static inline uint32_t espi_readl(struct espi_data *priv, uint32_t offset)
{
	return __raw_readl(priv->regs + offset);
}
static inline void espi_writel(struct espi_data *priv, uint32_t offset, uint32_t val)
{
	__raw_writel(val, priv->regs + offset);
}


#endif /* SPI_DB_ESPI_H */