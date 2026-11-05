#include "radio.h"

#include <stdio.h>
#include "sx126x.h"

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

  /* Power amplifier (PA) setup, as RadioLib configures it (13.1.14).
   * These values select and size the amplifier; the actual output power is
   * then set in dBm by set_tx_params below. Semtech's table lists these as
   * the optimal values for +22 dBm, so at 10 dBm the PA is less efficient
   * (draws more current) than a setting tuned for 10 dBm, but works fine. */
  const sx126x_pa_cfg_params_t pa_cfg = {
    .pa_duty_cycle = 0x04, /* PA on-time per cycle; SX1262 max is 0x04 */
    .hp_max = 0x07,        /* size of the high-power PA, 0x00..0x07 (max) */
    .device_sel = 0x00,    /* 0 = SX1262 (high-power PA), 1 = SX1261 */
    .pa_lut = 0x01,        /* reserved, always 0x01 */
  };

  /* Modulation: how each chirp is shaped. See the #defines at the top. */
  const sx126x_mod_params_lora_t mod_params = {
    .sf = RADIO_SF,
    .bw = RADIO_BW,
    .cr = RADIO_CR,
    /* Low data rate optimization: compensates for clock drift when chirps
     * are very long (over ~16 ms). Only needed for SF11/SF12 at 125 kHz. */
    .ldro = 0,
  };

  /* Packet format: what surrounds your data in each packet. */
  const sx126x_pkt_params_lora_t pkt_params = {
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

/* Datasheet: 14.2 "Circuit Configuration for Basic Tx Operation" lists
 * these steps in order. Most of the setup is already done in radio_init. */
int radio_transmit(const uint8_t *data, uint8_t len)
{
  /* TODO 1: Prepare (move to file scope).
   * You need pkt_params and mod_params here, but they're local consts in
   * radio_init. Move them above radio_init as `static` variables (not
   * const: step 4 changes the payload length). */

  /* TODO 2: Check the arguments.
   * Return -1 if data is NULL or len is 0. len is a uint8_t, so it can't
   * exceed 255, which is the most the chip's buffer holds. */

  /* TODO 3: Copy the payload into the radio's buffer.
   * The SX1262 has a 256-byte RAM buffer shared by TX and RX.
   * sx126x_set_buffer_base_address(NULL, tx_base, rx_base) says where TX
   * data starts; use 0 for both. That only needs to happen once, so it
   * could go in radio_init instead.
   * Then call sx126x_write_buffer(NULL, offset, data, len). Use offset = the
   * TX base you chose. */

  /* TODO 4: Tell the chip how long the packet is.
   * In explicit-header mode the chip sends exactly pld_len_in_bytes bytes,
   * so set pkt_params.pld_len_in_bytes = len, then call
   * sx126x_set_lora_pkt_params again. Forgetting this sends 255 bytes every
   * time (the value from radio_init): ~2 s of airtime instead of ~0.2 s. */

  /* TODO 5: Enable the IRQ flags you want to watch.
   * sx126x_set_dio_irq_params(NULL, irq_mask, dio1_mask, dio2_mask, dio3_mask)
   * - irq_mask: which events the chip records in its IRQ status register.
   *   You want SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT.
   * - dioN_mask: which of those events also drive a DIO pin high. You'll
   *   read the status over SPI (polling), so pass 0 for all three for now.
   *   Later, mapping TX_DONE to DIO1 and using an EXTI interrupt saves the
   *   CPU from waiting. (DIO2/DIO3 are already used for the antenna switch
   *   and TCXO.)
   * Like step 3, this can move to radio_init once it works.
   * Then sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL) so old flags don't
   * look like this packet finishing. */

  /* TODO 6: Start transmitting.
   * sx126x_set_tx(NULL, timeout_in_ms). The chip moves into TX, sends the
   * packet, raises TX_DONE, then returns to standby on its own.
   * The timeout is a safety net: if TX hasn't finished by then, the chip
   * stops and raises TIMEOUT. 0 means no timeout. A good value is the
   * packet's airtime plus some margin:
   *   sx126x_get_lora_time_on_air_in_ms(&pkt_params, &mod_params) + 100 */

  /* TODO 7: Wait for it to finish.
   * Loop: sx126x_get_irq_status(NULL, &irq) until irq has TX_DONE or TIMEOUT
   * set (check with &). Also keep your own deadline with HAL_GetTick() (needs
   * main.h) in case the chip never answers, so the loop can't hang forever.
   * Returning -1 on TIMEOUT or your deadline is fine.
   * Finally clear the flags with sx126x_clear_irq_status. */

  /* After TX the chip goes to its "fallback mode", STBY_RC by default, so
   * radio_print_status will show 2 instead of 3. That's expected. Use
   * sx126x_set_rx_tx_fallback_mode(NULL, SX126X_FALLBACK_STDBY_XOSC) in
   * radio_init to stay on the TCXO, which makes the next TX start faster. */

  (void)data;
  (void)len;
  return -1;
}
