#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* Prepares the radio before transmission/receiving.
 * Returns 0 on success, -1 on failure. */
int radio_init(void);

/* Transmits a packet containing len bytes from the radio.
 * Returns 0 on success, -1 on failure or timeout. */
int radio_transmit(const uint8_t *data, uint8_t len);

/* Print the radio's chip mode over the UART. */
void radio_print_status(void);

#endif /* RADIO_H */
