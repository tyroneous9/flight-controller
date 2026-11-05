#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* Bring the radio from power-on to ready: TCXO running, calibrated,
 * configured for LoRa (settings in radio.c), and in STBY_XOSC.
 * Returns 0 on success, -1 on failure. */
int radio_init(void);

/* Send one packet of len bytes (1..255) and wait until it has gone out.
 * Returns 0 on success, -1 on failure or timeout. */
int radio_transmit(const uint8_t *data, uint8_t len);

/* Print the radio's chip mode over the UART. */
void radio_print_status(void);

#endif /* RADIO_H */
