#include "radio.h"

#include <stdio.h>
#include "sx126x.h"

/* The Wio-SX1262's 32 MHz reference is a TCXO powered from DIO3.
 * 1.8V works; not yet checked against the module datasheet's rated voltage.
 * Delay is in 15.625 us steps: 5 ms / 15.625 us = 320. */
#define RADIO_TCXO_VOLTAGE  SX126X_TCXO_CTRL_1_8V
#define RADIO_TCXO_DELAY    320

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
 * 13.6.1/13.6.2 Get/ClearDeviceErrors. */
int radio_init(void)
{
  sx126x_errors_mask_t errors = 0;
  sx126x_chip_status_t status = {0};

  /* Clean state. Reset is also the only way to undo TCXO mode (4.1.4). */
  RADIO_CHECK(sx126x_reset(NULL));
  RADIO_CHECK(sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC));

  /* Power the TCXO from DIO3, then drop the XOSC_START flag left over from
   * power-on, when the TCXO had no power (13.3.6). */
  RADIO_CHECK(sx126x_set_dio3_as_tcxo_ctrl(NULL, RADIO_TCXO_VOLTAGE, RADIO_TCXO_DELAY));
  RADIO_CHECK(sx126x_clear_device_errors(NULL));

  /* The power-on calibration ran without a clock, so redo it. */
  RADIO_CHECK(sx126x_cal(NULL, SX126X_CAL_ALL));

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
