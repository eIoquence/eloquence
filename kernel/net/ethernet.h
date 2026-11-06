#ifndef ELOQUENCE_NET_ETHERNET_H
#define ELOQUENCE_NET_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>

#define ETH_ADDR_LEN 6U
#define ETH_HEADER_LEN 14U
#define ETH_MAX_FRAME 1514U
#define ETH_MIN_FRAME 60U

#define ETHERTYPE_ARP 0x0806U
#define ETHERTYPE_IPV4 0x0800U

struct ethernet_header {
    uint8_t dst[ETH_ADDR_LEN];
    uint8_t src[ETH_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed));

static inline uint16_t eth_be16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

/* Demultiplex a received Ethernet frame to ARP/IPv4. Called by the driver
 * for every frame taken from the virtio-net RX queue. */
bool ethernet_input(const uint8_t *frame, uint32_t length);

#endif
