#ifndef RADIO_H
#define RADIO_H

/* Bring the radio from power-on to ready: TCXO running, calibrated,
 * configured for LoRa (settings in radio.c), and in STBY_XOSC.
 * Returns 0 on success, -1 on failure. */
int radio_init(void);

/* Print the radio's chip mode over the UART. */
void radio_print_status(void);

#endif /* RADIO_H */
