#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arp.h"
#include "netif.h"

#define ARP_HEADER_LEN 28U
#define ARP_FRAME_LEN (ETH_HEADER_LEN + ARP_HEADER_LEN)

static struct arp_entry arp_cache[ARP_CACHE_SIZE];

static uint16_t arp_read_be16(const uint8_t *cursor) {
    return (uint16_t)((uint16_t)cursor[0] << 8 | cursor[1]);
}

static uint32_t arp_read_be32(const uint8_t *cursor) {
    return ((uint32_t)cursor[0] << 24) | ((uint32_t)cursor[1] << 16)
        | ((uint32_t)cursor[2] << 8) | (uint32_t)cursor[3];
}

static void arp_write_be16(uint8_t *cursor, uint16_t value) {
    cursor[0] = (uint8_t)(value >> 8);
    cursor[1] = (uint8_t)value;
}

static void arp_write_be32(uint8_t *cursor, uint32_t value) {
    cursor[0] = (uint8_t)(value >> 24);
    cursor[1] = (uint8_t)(value >> 16);
    cursor[2] = (uint8_t)(value >> 8);
    cursor[3] = (uint8_t)value;
}

static bool arp_is_broadcast(const uint8_t mac[ETH_ADDR_LEN]) {
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        if (mac[i] != 0xffU) {
            return false;
        }
    }
    return true;
}

static uint8_t arp_frame[ARP_FRAME_LEN];

static bool arp_send_frame(uint16_t opcode, const uint8_t target_mac[6],
                           uint32_t target_ip) {
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);

    struct ethernet_header *eth = (struct ethernet_header *)arp_frame;
    const uint8_t *dst = arp_is_broadcast(target_mac)
        ? (const uint8_t *)"\xff\xff\xff\xff\xff\xff" : target_mac;
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        eth->dst[i] = dst[i];
        eth->src[i] = our_mac[i];
    }
    eth->ethertype = eth_be16(ETHERTYPE_ARP);

    uint8_t *arp = arp_frame + ETH_HEADER_LEN;
    arp_write_be16(arp + 0, ARP_HTYPE_ETHERNET);
    arp_write_be16(arp + 2, ETHERTYPE_IPV4);
    arp[4] = ETH_ADDR_LEN;
    arp[5] = 4U;
    arp_write_be16(arp + 6, opcode);
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        arp[8 + i] = our_mac[i];        /* sender MAC */
        arp[14 + i] = target_mac[i];    /* target MAC */
    }
    arp_write_be32(arp + 14, our_ip);   /* sender IP */
    arp_write_be32(arp + 20, target_ip); /* target IP */

    if (!netif_transmit(arp_frame, ARP_FRAME_LEN)) {
        return false;
    }
    arp_cache_add(target_ip, target_mac);
    return true;
}

bool arp_input(const uint8_t *payload, uint32_t length) {
    if (payload == 0 || length < ARP_HEADER_LEN) {
        return false;
    }
    const uint8_t *arp = payload;
    uint16_t htype = arp_read_be16(arp + 0);
    uint16_t ptype = arp_read_be16(arp + 2);
    uint8_t hlen = arp[4];
    uint8_t plen = arp[5];
    uint16_t opcode = arp_read_be16(arp + 6);
    const uint8_t *sender_mac = arp + 8;
    uint32_t sender_ip = arp_read_be32(arp + 14);
    uint32_t target_ip = arp_read_be32(arp + 20);

    if (htype != ARP_HTYPE_ETHERNET || ptype != ETHERTYPE_IPV4
        || hlen != ETH_ADDR_LEN || plen != 4U) {
        return false;
    }

    /* Cache the sender mapping regardless of opcode. */
    arp_cache_add(sender_ip, sender_mac);

    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);
    if (target_ip != our_ip) {
        return true; /* not for us */
    }

    if (opcode == ARP_OP_REQUEST) {
        return arp_send_frame(ARP_OP_REPLY, sender_mac, sender_ip);
    }
    return true;
}

bool arp_resolve(uint32_t ip, uint8_t mac[ETH_ADDR_LEN]) {
    if (mac == 0) {
        return false;
    }
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].in_use && arp_cache[i].ip == ip) {
            for (uint32_t j = 0; j < ETH_ADDR_LEN; j++) {
                mac[j] = arp_cache[i].mac[j];
            }
            return true;
        }
    }
    return false;
}

void arp_cache_add(uint32_t ip, const uint8_t mac[ETH_ADDR_LEN]) {
    if (mac == 0 || ip == 0) {
        return;
    }
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].in_use && arp_cache[i].ip == ip) {
            for (uint32_t j = 0; j < ETH_ADDR_LEN; j++) {
                arp_cache[i].mac[j] = mac[j];
            }
            return;
        }
    }
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].in_use) {
            arp_cache[i].in_use = true;
            arp_cache[i].ip = ip;
            for (uint32_t j = 0; j < ETH_ADDR_LEN; j++) {
                arp_cache[i].mac[j] = mac[j];
            }
            return;
        }
    }
    /* Replace the oldest entry. */
    arp_cache[0].in_use = true;
    arp_cache[0].ip = ip;
    for (uint32_t j = 0; j < ETH_ADDR_LEN; j++) {
        arp_cache[0].mac[j] = mac[j];
    }
}

bool arp_request(uint32_t target_ip) {
    uint8_t broadcast[ETH_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    return arp_send_frame(ARP_OP_REQUEST, broadcast, target_ip);
}
