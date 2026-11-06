#include "arp.h"
#include "ethernet.h"
#include "ipv4.h"
#include "netif.h"

bool ethernet_input(const uint8_t *frame, uint32_t length) {
    if (frame == 0 || length < ETH_HEADER_LEN) {
        return false;
    }
    const struct ethernet_header *eth = (const struct ethernet_header *)frame;
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);

    bool for_us = true;
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        if (eth->dst[i] != our_mac[i] && eth->dst[i] != 0xffU) {
            for_us = false;
            break;
        }
    }
    if (!for_us) {
        return false;
    }

    const uint8_t *payload = frame + ETH_HEADER_LEN;
    uint32_t payload_length = length - ETH_HEADER_LEN;
    uint16_t ethertype = eth_be16(eth->ethertype);

    if (ethertype == ETHERTYPE_ARP) {
        return arp_input(payload, payload_length);
    }
    if (ethertype == ETHERTYPE_IPV4) {
        return ipv4_input(eth->src, payload, payload_length);
    }
    return false;
}
