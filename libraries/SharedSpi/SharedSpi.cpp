// ASF 3.27.0

/**
 * \file
 *
 * \brief Shared SPI bus services for Duet and other Atmel SAM-based controller electronics
 *
 * This module provides access to the SPI bus used to access peripheral devices in RepRapFirmware, in particular thermocouple and RTD readers.
 * Depending on the board, we may use either the main SPI channel or one of the USARTs in SPI mode.
 *
 */

#include "SharedSpi.h"
#include "Arduino.h"
#include "compiler.h"
#include "variant.h"

#if SAM4E

#include "usart/usart.h"		// On Duet NG the general SPI channel is on a USART

#define USART_SSPI	USART1		//TODO change to USART0 for the second prototype
#define SSPI_ID		ID_USART1	//TODO change to USTART0

#else

/**
 * \brief Max number when the chip selects are connected to a 4- to 16-bit decoder.
 */
# define MAX_NUM_WITH_DECODER 0x10

/**
 * \brief Max number when the chip selects are directly connected to peripheral device.
 */
# define MAX_NUM_WITHOUT_DECODER 0x04

/**
 * \brief Max number when the chip selects are directly connected to peripheral device.
 */
# define NONE_CHIP_SELECT_ID 0x0f

/**
 * \brief The default chip select id.
 */
# define DEFAULT_CHIP_ID 1

/** Time-out value (number of attempts). */
#define SPI_TIMEOUT       15000

// Which SPI channel we use
# define SSPI	SPI0

#endif

// Lock for the SPI subsystem
static bool sspiLocked = false;

// Gain exclusive use of the GSPI bus
// Returning true if successful, false if GSPI bus is busy
bool sspi_acquire()
{
	irqflags_t flags = cpu_irq_save();
	bool rslt;
	if (sspiLocked)
	{
		rslt = false;
	}
	else
	{
		sspiLocked = true;
		rslt = true;
	}
	cpu_irq_restore(flags);
	return rslt;
}

// Release the GSPI bus
void sspi_release()
{
	sspiLocked = false;
}

// Wait for transmitter ready returning true if timed out
static inline bool waitForTxReady()
{
#if SAM4E
	uint32_t timeout = SPI_TIMEOUT;
	while (!usart_is_tx_ready(USART_SSPI))
	{
		if (--timeout == 0)
		{
			return true;
		}
	}
	return false;
#else
	uint32_t timeout = SPI_TIMEOUT;
	while (!spi_is_tx_ready(SSPI))
	{
		if (--timeout == 0)
		{
			return true;
		}
	}
	return false;
#endif
}

// Wait for transmitter empty returning true if timed out
static inline bool waitForTxEmpty()
{
#if SAM4E
	uint32_t timeout = SPI_TIMEOUT;
	while (!usart_is_tx_empty(USART_SSPI))
	{
		if (!timeout--)
		{
			return true;
		}
	}
	return false;

#else
	uint32_t timeout = SPI_TIMEOUT;
	while (!spi_is_tx_empty(SSPI))
	{
		if (!timeout--)
		{
			return true;
		}
	}
	return false;
#endif
}

// Wait for receive data available returning true if timed out
static inline bool waitForRxReady()
{
#if SAM4E
	uint32_t timeout = SPI_TIMEOUT;
	while (!usart_is_rx_ready(USART_SSPI))
	{
		if (--timeout == 0)
		{
			return true;
		}
	}
	return false;
#else
	uint32_t timeout = SPI_TIMEOUT;
	while (!spi_is_rx_ready(SSPI))
	{
		if (--timeout == 0)
		{
			return true;
		}
	}
	return false;
#endif
}

// Set up the GSPI subsystem
void sspi_master_init(struct sspi_device *device, uint32_t bits)
{
	static bool init_comms = true;

	if (init_comms)
	{
#if SAM4E
		//TODO change the following to USART0 for the second prototype
		ConfigurePin(g_APinDescription[APIN_USART1_SCK]);
		ConfigurePin(g_APinDescription[APIN_USART1_MOSI]);
		ConfigurePin(g_APinDescription[APIN_USART1_MISO]);

		pmc_enable_periph_clk(SSPI_ID);

		// Set USART0 in SPI master mode
		USART_SSPI->US_IDR = ~0u;
		USART_SSPI->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
		USART_SSPI->US_MR = US_MR_USART_MODE_SPI_MASTER
						| US_MR_USCLKS_MCK
						| US_MR_CHRL_8_BIT
						| US_MR_CHMODE_NORMAL;
		USART_SSPI->US_BRGR = SystemCoreClock/1000000;			// 1MHz SPI clock for now
		USART_SSPI->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS | US_CR_RSTSTA;
#else
		ConfigurePin(g_APinDescription[APIN_SPI_SCK]);
		ConfigurePin(g_APinDescription[APIN_SPI_MOSI]);
		ConfigurePin(g_APinDescription[APIN_SPI_MISO]);

		pmc_enable_periph_clk(SPI_INTERFACE_ID);

		spi_reset(SSPI);

		// set master mode, peripheral select, disable fault detection
		spi_set_master_mode(SSPI);
		spi_disable_mode_fault_detect(SSPI);
		spi_disable_loopback(SSPI);
		spi_set_peripheral_chip_select_value(SSPI, DEFAULT_CHIP_ID);
		spi_set_fixed_peripheral_select(SSPI);
		spi_disable_peripheral_select_decode(SSPI);

# if defined(USE_SAM3X_DMAC)
	pmc_enable_periph_clk(ID_DMAC);
	dmac_disable(DMAC);
	dmac_set_priority_mode(DMAC, DMAC_GCFG_ARB_CFG_FIXED);
	dmac_enable(DMAC);
# endif

#endif
		init_comms = false;
	}

#if SAM4E
	// On USARTs we only support 8-bit transfers. 5, 6, 7 and 9 are also available.
	device->bits = US_MR_CHRL_8_BIT;
#else
	// For now we only support 8 and 16 bit modes. 11-15 bit modes are also available.
	switch (bits)
	{
	case 8:
	default:
		device->bits = SPI_CSR_BITS_8_BIT;
		break;
	case 16:
		device->bits = SPI_CSR_BITS_16_BIT;
		break;
	}
#endif
}

/**
 * \brief Set up an SPI device.
 *
 * The returned device descriptor structure must be passed to the driver
 * whenever that device should be used as current slave device.
 *
 * \param device    Pointer to SPI device struct that should be initialized.
 * \param flags     SPI configuration flags. Common flags for all
 *                  implementations are the SPI modes SPI_MODE_0 ...
 *                  SPI_MODE_3.
 * \param baud_rate Baud rate for communication with slave device in Hz.
 * \param sel_id    Board specific select id.
 */
void sspi_master_setup_device(const struct sspi_device *device, uint8_t spiMode, uint32_t baud_rate)
{
#if SAM4E
	USART_SSPI->US_CR = US_CR_RSTRX | US_CR_RSTTX;	// reset transmitter and receiver
	uint32_t mr = US_MR_USART_MODE_SPI_MASTER
					| US_MR_USCLKS_MCK
					| US_MR_CHRL_8_BIT
					| US_MR_CHMODE_NORMAL
					| US_MR_CLKO;
	if (spiMode & 2)
	{
		mr |= US_MR_CPOL;
	}
	if (spiMode & 1)
	{
		mr |= US_MR_CPHA;
	}
	USART_SSPI->US_MR = mr;
	USART_SSPI->US_BRGR = SystemCoreClock/baud_rate;
	USART_SSPI->US_CR = US_CR_RXEN | US_CR_TXEN;	// enable transmitter and receiver
#else
	spi_reset(SSPI);
	spi_set_master_mode(SSPI);
	spi_set_bits_per_transfer(SSPI, device->id, device->bits);
	int16_t baud_div = spi_calc_baudrate_div(baud_rate, SystemCoreClock);
	spi_set_baudrate_div(SSPI, device->id, baud_div);
	spi_set_clock_polarity(SSPI, device->id, spiMode >> 1);
	spi_set_clock_phase(SSPI, device->id, ((spiMode & 0x1) ^ 0x1));
	spi_enable(SSPI);
#endif
}

/**
 * \brief Select the given device on the SPI bus.
 *
 * Set device specific setting and call board chip select.
 *
 * \param device  SPI device.
 *
 */
void sspi_select_device(const struct sspi_device *device)
{
#if SAM3XA
	if (device->id < MAX_NUM_WITHOUT_DECODER)
	{
		spi_set_peripheral_chip_select_value(SSPI, (~(1 << device->id)));
	}
#endif

	// Enable the CS line
	digitalWrite(device->csPin, LOW);
}

/**
 * \brief Deselect the given device on the SPI bus.
 *
 * Call board chip deselect.
 *
 * \param device  SPI device.
 *
 * \pre SPI device must be selected with spi_select_device() first.
 */
void sspi_deselect_device(const struct sspi_device *device)
{
	waitForTxEmpty();

#if SAM3XA
	// Last transfer, so de-assert the current NPCS if CSAAT is set.
	spi_set_lastxfer(SSPI);

	// Assert all lines; no peripheral is selected.
	spi_set_peripheral_chip_select_value(SSPI, NONE_CHIP_SELECT_ID);
#endif

	// Disable the CS line
	digitalWrite(device->csPin, HIGH);
}

/**
 * \brief Send and receive a sequence of bytes from an SPI device.
 *
 * \param tx_data   Data buffer to send.
 * \param rx_data   Data buffer to read.
 * \param len       Length of data to be read.
 *
 * \pre SPI device must be selected with spi_select_device() first.
 */
spi_status_t sspi_transceive_packet(uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
	for (uint32_t i = 0; i < len; ++i)
	{
		if (waitForTxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Write to transmit register
		uint32_t dOut = (tx_data == nullptr) ? 0x000000FF : (uint32_t)tx_data[i];
		if (i + 1 == len)
		{
			dOut |= SPI_TDR_LASTXFER;
		}

#if SAM4E
		USART_SSPI->US_THR = dOut;
#else
		SSPI->SPI_TDR = dOut;
#endif
		// Wait for receive register
		if (waitForRxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Get data from receive register
		uint8_t dIn =
#if SAM4E
				(uint8_t)USART_SSPI->US_RHR;
#else
				(uint8_t)SSPI->SPI_RDR;
#endif
		if (rx_data != nullptr)
		{
			rx_data[i] = dIn;
		}
	}

	return SPI_OK;
}

#if SAM3XA
/**
 * \brief Send and receive a sequence of 16-bit words from an SPI device.
 *
 * \param tx_data   Data buffer to send.
 * \param rx_data   Data buffer to read.
 * \param len       Length of data to send and receive in 16-bit words.
 *
 * \pre SPI device must be selected with spi_select_device() first.
 */
spi_status_t sspi_transceive_packet16(uint16_t *tx_data, uint16_t *rx_data, size_t len)
{
	for (uint32_t i = 0; i < len; ++i)
	{
		// Wait for transmit register empty
		if (waitForTxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Write to transmit register
		uint32_t dOut = (tx_data == nullptr) ? 0x000000FF : (uint32_t)tx_data[i];
		if (i + 1 == len)
		{
			dOut |= SPI_TDR_LASTXFER;
		}
		SSPI->SPI_TDR = dOut;

		// Wait for receive register
		if (waitForRxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Get data from receive register
		uint16_t dIn = (uint16_t)SSPI->SPI_RDR;
		if (rx_data != nullptr)
		{
			rx_data[i] = dIn;
		}
	}

	return SPI_OK;
}
#endif

#if defined(USE_SAM3X_DMAC)

void sspi_start_transmit_dma(Dmac *p_dmac, uint32_t ul_num, const void *src, uint32_t nb_bytes)
{
	static uint8_t ff = 0xFF;
	uint32_t cfg, src_incr = DMAC_CTRLB_SRC_INCR_INCREMENTING;
	dma_transfer_descriptor_t desc;

	// Send 0xFF repeatedly if src is NULL
	if (!src) {
		src = &ff;
		src_incr = DMAC_CTRLB_SRC_INCR_FIXED;
	}

	// Disable the DMA channel prior to configuring
	dmac_enable(p_dmac);
	dmac_channel_disable(p_dmac, ul_num);

	cfg = DMAC_CFG_SOD
		| DMAC_CFG_DST_H2SEL
		| DMAC_CFG_DST_PER(SPI_TX_IDX)
		| DMAC_CFG_FIFOCFG_ALAP_CFG;
	dmac_channel_set_configuration(p_dmac, ul_num, cfg);

	// Prepare DMA transfer
	desc.ul_source_addr = (uint32_t)src;
	desc.ul_destination_addr = (uint32_t)&(SSPI->SPI_TDR);
	desc.ul_ctrlA = DMAC_CTRLA_BTSIZE(nb_bytes)
		| DMAC_CTRLA_SRC_WIDTH_BYTE
		| DMAC_CTRLA_DST_WIDTH_BYTE;
	desc.ul_ctrlB = DMAC_CTRLB_SRC_DSCR
		| DMAC_CTRLB_DST_DSCR
		| DMAC_CTRLB_FC_MEM2PER_DMA_FC
		| src_incr
		| DMAC_CTRLB_DST_INCR_FIXED;

	// Next field is ignored, but set it anyway
	desc.ul_descriptor_addr = (uint32_t)NULL;

 	// Finish configuring the transfer
	dmac_channel_single_buf_transfer_init(p_dmac, ul_num, &desc);

	// And now start the DMA transfer
	dmac_channel_enable(p_dmac, ul_num);
}

void sspi_start_receive_dma(Dmac *p_dmac, uint32_t ul_num, const void *dest, uint32_t nb_bytes)
{
	uint32_t cfg;
	dma_transfer_descriptor_t desc;

	// clear any potential overrun error
	cfg = SSPI->SPI_SR;

	// Turn the DMA channel off before configuring
	dmac_enable(p_dmac);
	dmac_channel_disable(p_dmac, ul_num);

	cfg = DMAC_CFG_SOD
		| DMAC_CFG_SRC_H2SEL
		| DMAC_CFG_SRC_PER(SPI_RX_IDX)
		| DMAC_CFG_FIFOCFG_ASAP_CFG;
	dmac_channel_set_configuration(p_dmac, ul_num, cfg);

	// Prepare DMA transfer
	desc.ul_source_addr = (uint32_t)&(SSPI->SPI_RDR);
	desc.ul_destination_addr = (uint32_t)dest;
	desc.ul_ctrlA = DMAC_CTRLA_BTSIZE(nb_bytes)
		| DMAC_CTRLA_SRC_WIDTH_BYTE
		| DMAC_CTRLA_DST_WIDTH_BYTE;
	desc.ul_ctrlB = DMAC_CTRLB_SRC_DSCR
		| DMAC_CTRLB_DST_DSCR
		| DMAC_CTRLB_FC_PER2MEM_DMA_FC
		| DMAC_CTRLB_SRC_INCR_FIXED
		| DMAC_CTRLB_DST_INCR_INCREMENTING;

	// This next field is ignored but set it anyway
	desc.ul_descriptor_addr = (uint32_t)NULL;

	// Finish configuring the DMA transfer
	dmac_channel_single_buf_transfer_init(p_dmac, ul_num, &desc);

	// And now allow the DMA transfer to begin
	dmac_channel_enable(p_dmac, ul_num);
}

#endif

//! @}
