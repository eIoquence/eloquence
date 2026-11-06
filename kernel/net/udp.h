#ifndef ELOQUENCE_NET_UDP_H
#define ELOQUENCE_NET_UDP_H

#include <stdbool.h>
#include <stdint.h>

#include "ethernet.h"
#include "socket.h"

/* Procesează un datagram UDP primit (payload IPv4). Caută socket-ul
 * destinatar și-l livrează în coada RX. Returnează true dacă frame-ul
 * era UDP și a fost gestionat. */
bool udp_input(const uint8_t eth_src[ETH_ADDR_LEN], const uint8_t *payload,
               uint32_t length);

/* Trimite un datagram UDP către destinație. */
bool udp_output(uint32_t destination_ip, uint16_t destination_port,
                uint16_t source_port, const uint8_t *payload,
                uint32_t payload_length);

/* Calculează checksum-ul UDP (cu pseudo-header). */
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t *data, uint32_t length);

#endif
