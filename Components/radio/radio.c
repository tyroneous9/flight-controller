#include "radio.h"

#include <stdio.h>
#include "sx126x.h"

/* The Wio-SX1262's 32 MHz reference is a TCXO powered from DIO3. 
 * This can be removed later when replacing this module with a cheaper one that has a crystal.
 * 1.8V works; not yet checked against the module datasheet's rated voltage.
 * Delay is in 15.625 us steps: 5 ms / 15.625 us = 320. */
#define RADIO_TCXO_VOLTAGE  SX126X_TCXO_CTRL_1_8V
#define RADIO_TCXO_DELAY    320

/* Frequency and the band used for image calibration. Must be legal where you
 * fly and match the receiver. US 902-928 MHz; for EU use 868 MHz / 863-870. */
#define RADIO_FREQ_HZ       915000000UL
#define RADIO_BAND_MIN_MHZ  902
#define RADIO_BAND_MAX_MHZ  928

/* LoRa settings, using RadioLib's SX126x defaults. Both radios must match. */
#define RADIO_SF            SX126X_LORA_SF9
#define RADIO_BW            SX126X_LORA_BW_125
#define RADIO_CR            SX126X_LORA_CR_4_7
#define RADIO_PREAMBLE_LEN  8
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
  /* SX1262 high-power PA, as RadioLib configures it; TX power is then set
   * directly in dBm by set_tx_params (13.1.14). */
  const sx126x_pa_cfg_params_t pa_cfg = {
    .pa_duty_cycle = 0x04,
    .hp_max = 0x07,
    .device_sel = 0x00,
    .pa_lut = 0x01,
  };
  const sx126x_mod_params_lora_t mod_params = {
    .sf = RADIO_SF,
    .bw = RADIO_BW,
    .cr = RADIO_CR,
    .ldro = 0, /* only needed for SF11/SF12 at 125 kHz */
  };
  const sx126x_pkt_params_lora_t pkt_params = {
    .preamble_len_in_symb = RADIO_PREAMBLE_LEN,
    .header_type = SX126X_LORA_PKT_EXPLICIT,
    .pld_len_in_bytes = 255, /* set again for each packet on transmit */
    .crc_is_on = true,
    .invert_iq_is_on = false,
  };

  /* Clean state. Reset is also the only way to undo TCXO mode (4.1.4). */
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

  RADIO_CHECK(sx126x_set_pa_cfg(NULL, &pa_cfg));
  RADIO_CHECK(sx126x_set_tx_params(NULL, RADIO_TX_POWER_DBM, SX126X_RAMP_200_US));

  /* The Wio-SX1262's antenna switch is driven by DIO2. */
  RADIO_CHECK(sx126x_set_dio2_as_rf_sw_ctrl(NULL, true));

  /* Switch to the 32 MHz reference, then verify: no errors and mode 3. */
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
