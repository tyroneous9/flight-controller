#include "radio.h"

#include <stdio.h>
#include "sx126x.h"
#include "main.h"

/* ---------------------------------------------------------------------------
 * Clock (TCXO)
 * ------------------------------------------------------------------------- */

/* The Wio-SX1262's 32 MHz reference is a TCXO powered from DIO3.
 * This can be removed later when replacing this module with a cheaper one that has a crystal.
 * 1.8V works; not yet checked against the module datasheet's rated voltage.
 * Delay is in 15.625 us steps: 5 ms / 15.625 us = 320. */
#define RADIO_TCXO_VOLTAGE  SX126X_TCXO_CTRL_1_8V
#define RADIO_TCXO_DELAY    320

/* ---------------------------------------------------------------------------
 * Frequency
 * ------------------------------------------------------------------------- */

/* Center frequency of the signal. Both radios must use the same one, and it
 * must be inside a band that's legal where you fly:
 *   US: 902-928 MHz (FCC Part 15)   EU: 863-870 MHz (868 MHz is common)
 * The band limits are only used for image calibration: the receiver is tuned
 * to reject a "mirror" frequency, and that tuning depends on the band. */
#define RADIO_FREQ_HZ       915000000UL
#define RADIO_BAND_MIN_MHZ  902
#define RADIO_BAND_MAX_MHZ  928

/* ---------------------------------------------------------------------------
 * LoRa modulation (RadioLib's SX126x defaults). Both radios must match.
 * ------------------------------------------------------------------------- */

/* Spreading factor: bits per chirp, SF5..SF12. Each step up doubles the time
 * per chirp (slower) and lets the receiver hear a signal ~2.5 dB weaker
 * (more range). SF9 at 125 kHz: 4.1 ms per chirp, ~226 ms for 20 bytes. */
#define RADIO_SF            SX126X_LORA_SF9

/* Bandwidth: how wide a range of frequencies each chirp sweeps.
 * Wider = faster but shorter range; 125 kHz is the most common choice.
 * Options include 125, 250 and 500 kHz (and narrower ones below 125). */
#define RADIO_BW            SX126X_LORA_BW_125

/* Coding rate: error-correction overhead. 4/7 sends every 4 data bits as 7.
 * Options 4/5 (fastest, least protection) .. 4/8 (slowest, most).
 * Helps against short interference bursts; adds little range. */
#define RADIO_CR            SX126X_LORA_CR_4_7

/* Preamble: plain chirps at the start of each packet that let the receiver
 * notice a packet and lock onto its timing. 8 is standard. Longer makes
 * packets easier to catch but costs airtime. Both sides should match. */
#define RADIO_PREAMBLE_LEN  8

/* Transmit power in dBm (10 dBm = 10 mW). The SX1262's high-power PA allows
 * -9..+22 dBm. +3 dB = 2x power; +6 dB = ~2x range in open space.
 * Check your region's legal limit before raising it. */
#define RADIO_TX_POWER_DBM  10

/* Return -1 from the calling function if an sx126x_* call fails, naming it. */
#define RADIO_CHECK(call)                                 \
  do                                                      \
  {                                                       \
    if ((call) != SX126X_STATUS_OK)                       \
    {                                                     \
      printf("Radio call failed: %s\r\n", #call);         \
      return -1;                                          \
    }                                                     \
  } while (0)

/* Power amplifier (PA) setup, as RadioLib configures it (13.1.14).
  * These values select and size the amplifier; the actual output power is
  * then set in dBm by set_tx_params below. Semtech's table lists these as
  * the optimal values for +22 dBm, so at 10 dBm the PA is less efficient
  * (draws more current) than a setting tuned for 10 dBm, but works fine. */
static sx126x_pa_cfg_params_t pa_cfg = {
  .pa_duty_cycle = 0x04, /* PA on-time per cycle; SX1262 max is 0x04 */
  .hp_max = 0x07,        /* size of the high-power PA, 0x00..0x07 (max) */
  .device_sel = 0x00,    /* 0 = SX1262 (high-power PA), 1 = SX1261 */
  .pa_lut = 0x01,        /* reserved, always 0x01 */
};

/* Modulation: how each chirp is shaped. */
static sx126x_mod_params_lora_t mod_params = {
  .sf = RADIO_SF,
  .bw = RADIO_BW,
  .cr = RADIO_CR,
  /* Low data rate optimization: compensates for clock drift when chirps
    * are very long (over ~16 ms). Only needed for SF11/SF12 at 125 kHz. */
  .ldro = 0,
};

/* Packet format: what surrounds your data in each packet. */
static sx126x_pkt_params_lora_t pkt_params = {
  .preamble_len_in_symb = RADIO_PREAMBLE_LEN,
  /* Explicit: each packet carries a small header with its length, coding
    * rate and CRC setting, so the receiver doesn't need to know them in
    * advance. Implicit saves airtime but both sides must agree on them. */
  .header_type = SX126X_LORA_PKT_EXPLICIT,
  /* Payload length in bytes (max 255). Set again for each packet on
    * transmit; in RX with an explicit header it's read from the header. */
  .pld_len_in_bytes = 255,
  /* Checksum on the payload, so the receiver can drop corrupted packets. */
  .crc_is_on = true,
  /* Inverted IQ flips the chirp direction. LoRaWAN uses it so nodes don't
    * hear each other; for a point-to-point link leave it off on both sides. */
  .invert_iq_is_on = false,
};

void radio_print_status(void)
{
  sx126x_chip_status_t status = {0};
  sx126x_status_t get_status = sx126x_get_status(NULL, &status);
  if(get_status != SX126X_STATUS_OK)
  {
    printf("Failed to get status: %d\r\n", get_status);
  }
  else
  {
    printf("Radio status: %d\r\n", status.chip_mode);
  }
  /* chip status reference:
    2 = STBY_RC   3 = STBY_XOSC   4 = FS   5 = RX   6 = TX
   */
}

/* Datasheet: 4.1.4 TCXO, 13.3.6 SetDIO3AsTCXOCtrl, 13.1.12 Calibrate,
 * 13.6.1/13.6.2 Get/ClearDeviceErrors. Settings not set here (sync word,
 * regulator mode, current limit, ...) are left at the chip's defaults. */
int radio_init(void)
{
  sx126x_errors_mask_t errors = 0;
  sx126x_chip_status_t status = {0};

  /* Clean state. Reset is also the only way to undo TCXO mode (4.1.4).
   * STBY_RC = standby on the internal RC clock; most setup commands below
   * (packet type, calibration) must be sent in this mode. */
  RADIO_CHECK(sx126x_reset(NULL));
  RADIO_CHECK(sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC));

  /* Power the TCXO from DIO3, then drop the XOSC_START flag left over from
   * power-on, when the TCXO had no power (13.3.6). */
  RADIO_CHECK(sx126x_set_dio3_as_tcxo_ctrl(NULL, RADIO_TCXO_VOLTAGE, RADIO_TCXO_DELAY));
  RADIO_CHECK(sx126x_clear_device_errors(NULL));

  /* The power-on calibration ran without a clock, so redo it. */
  RADIO_CHECK(sx126x_cal(NULL, SX126X_CAL_ALL));

  /* LoRa setup. The chip starts in FSK mode, and the packet type must be set
   * first: switching it discards the other modulation settings (13.4.2). */
  RADIO_CHECK(sx126x_set_pkt_type(NULL, SX126X_PKT_TYPE_LORA));

  /* Frequency, then tune the receiver's image rejection for this band. */
  RADIO_CHECK(sx126x_set_rf_freq(NULL, RADIO_FREQ_HZ));
  RADIO_CHECK(sx126x_cal_img_in_mhz(NULL, RADIO_BAND_MIN_MHZ, RADIO_BAND_MAX_MHZ));

  RADIO_CHECK(sx126x_set_lora_mod_params(NULL, &mod_params));
  RADIO_CHECK(sx126x_set_lora_pkt_params(NULL, &pkt_params));

  /* Ramp time: how quickly the PA turns on and off at the start and end of
   * a packet. Switching too abruptly spreads energy into nearby frequencies;
   * 200 us is a common, safe choice. */
  RADIO_CHECK(sx126x_set_pa_cfg(NULL, &pa_cfg));
  RADIO_CHECK(sx126x_set_tx_params(NULL, RADIO_TX_POWER_DBM, SX126X_RAMP_200_US));

  /* The Wio-SX1262's antenna switch is driven by DIO2: the chip connects the
   * antenna to the TX path while transmitting and to RX otherwise. */
  RADIO_CHECK(sx126x_set_dio2_as_rf_sw_ctrl(NULL, true));

  /* Switch to the 32 MHz reference (STBY_XOSC): the TCXO stays running, so
   * later switches to TX or RX don't wait for it to start. Then verify: no
   * errors and mode 3. */
  RADIO_CHECK(sx126x_set_standby(NULL, SX126X_STANDBY_CFG_XOSC));
  RADIO_CHECK(sx126x_get_device_errors(NULL, &errors));
  RADIO_CHECK(sx126x_get_status(NULL, &status));

  if (errors != 0 || status.chip_mode != SX126X_CHIP_MODE_STBY_XOSC)
  {
    /* 0x0020 (XOSC_START): the TCXO didn't start. Check RADIO_TCXO_VOLTAGE
     * and RADIO_TCXO_DELAY against the module's TCXO. */
    printf("Radio init: errors = 0x%04X, chip mode = %d\r\n", errors, status.chip_mode);
    return -1;
  }

  return 0;
}

/* Transmits 8-bit data from the radio. */
int radio_transmit(const uint8_t *data, uint8_t len)
{
  /* len is a uint8_t, so it can't exceed 255: the most the buffer holds. */
  if (data == NULL || len == 0)
  {
    return -1;
  }

  /* The SX1262 has one 256-byte RAM buffer shared by TX and RX. Start the payload from the buffer start. */
  RADIO_CHECK(sx126x_set_buffer_base_address(NULL, 0, 0));
  RADIO_CHECK(sx126x_write_buffer(NULL, 0, data, len));

  /* The chip sends exactly pld_len_in_bytes bytes, so the params should be updated per packet.
   * Otherwise every packet would be 255 bytes (default). */
  pkt_params.pld_len_in_bytes = len;
  RADIO_CHECK(sx126x_set_lora_pkt_params(NULL, &pkt_params));

  /* Setting to record only TX_DONE and TIMEOUT in the IRQ status register, then clear old flags. */
  RADIO_CHECK(sx126x_set_dio_irq_params(NULL, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, 0, 0, 0));
  RADIO_CHECK(sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL));

  /* Expected air time is calculated from the LoRa formula (SF, BW, CR, preamble, payload length).
   * Must come after pld_len_in_bytes is set. */
  uint32_t expected_airtime = sx126x_get_lora_time_on_air_in_ms(&pkt_params, &mod_params);
  RADIO_CHECK(sx126x_set_tx(NULL, expected_airtime + 100));

  /* Poll the IRQ status until TX finishes or the chip times out */
  sx126x_irq_mask_t status = 0;
  uint32_t start = HAL_GetTick();
  while(1)
  {
    RADIO_CHECK(sx126x_get_irq_status(NULL, &status));

    if((status & SX126X_IRQ_TX_DONE) != 0)
    {
      RADIO_CHECK(sx126x_clear_irq_status(NULL, status));
      break;
    }

    /* Transmit deadline due to bad response */
    if((status & SX126X_IRQ_TIMEOUT) != 0)
    {
      return -1;
    }

    /* Transmit deadline due to no response */
    if(HAL_GetTick() - start > expected_airtime + 200)
    {
      return -1;
    }
  }

  return 0;
}
