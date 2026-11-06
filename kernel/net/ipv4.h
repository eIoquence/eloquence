#ifndef ELOQUENCE_NET_IPV4_H
#define ELOQUENCE_NET_IPV4_H

#include <stdbool.h>
#include <stdint.h>

#include "arp.h"
#include "ethernet.h"
#include "netif.h"

#define IPV4_HEADER_LEN 20U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_PROTOCOL_UDP 17U

/* Process a received IPv4 frame (Ethernet payload). Handles ICMP echo
 * requests by sending an echo reply. Returns true if the frame was IPv4 and
 * handled. */
bool ipv4_input(const uint8_t eth_src[ETH_ADDR_LEN],
                const uint8_t *payload, uint32_t length);

/* Transmit an IPv4 packet: resolves the next-hop MAC via ARP (broadcasting
 * an ARP request if needed), builds the IPv4 header and Ethernet frame, and
 * submits it to the interface. */
bool ipv4_output(uint8_t protocol, uint32_t destination_ip,
                 const uint8_t *payload, uint32_t payload_length);

/* Send an ICMP echo request to the given IPv4 address (resolving it via ARP
 * first). Returns true if the frame was submitted to the interface. */
bool ipv4_send_icmp_echo(uint32_t destination_ip);

#endif
