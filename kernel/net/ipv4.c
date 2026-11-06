#include <stdint.h>

#include "arp.h"
#include "firewall.h"
#include "ipv4.h"
#include "netif.h"
#include "udp.h"

#define IPV4_BROADCAST 0xffffffffU

static uint16_t ipv4_identification = 0x1000U;

static uint16_t ipv4_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    uint32_t i = 0;
    while (i + 1 < length) {
        sum += (uint32_t)((uint16_t)data[i] << 8 | data[i + 1]);
        i += 2;
    }
    if (i < length) {
        sum += (uint32_t)data[i] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* One's complement checksum used by ICMP (identical algorithm to IPv4). */
static uint16_t icmp_checksum(const uint8_t *data, uint32_t length) {
    return ipv4_checksum(data, length);
}

static uint32_t ipv4_read_be32(const uint8_t *cursor) {
    return ((uint32_t)cursor[0] << 24) | ((uint32_t)cursor[1] << 16)
        | ((uint32_t)cursor[2] << 8) | (uint32_t)cursor[3];
}

static void ipv4_write_be32(uint8_t *cursor, uint32_t value) {
    cursor[0] = (uint8_t)(value >> 24);
    cursor[1] = (uint8_t)(value >> 16);
    cursor[2] = (uint8_t)(value >> 8);
    cursor[3] = (uint8_t)value;
}

static void ipv4_write_be16(uint8_t *cursor, uint16_t value) {
    cursor[0] = (uint8_t)(value >> 8);
    cursor[1] = (uint8_t)value;
}

bool ipv4_input(const uint8_t eth_src[ETH_ADDR_LEN],
                const uint8_t *payload, uint32_t length) {
    if (eth_src == 0 || payload == 0 || length < IPV4_HEADER_LEN) {
        return false;
    }
    uint8_t version_ihl = payload[0];
    if ((version_ihl >> 4) != 4U || (version_ihl & 0x0fU) < 5U) {
        return false;
    }
    uint32_t header_length = (uint32_t)(version_ihl & 0x0fU) * 4U;
    if (length < header_length) {
        return false;
    }
    uint8_t protocol = payload[9];
    uint32_t source_ip = ipv4_read_be32(payload + 12);
    uint32_t destination_ip = ipv4_read_be32(payload + 16);
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);
    if (destination_ip != our_ip) {
        return false;
    }
    if (!firewall_check(source_ip, destination_ip, protocol)) {
        return true; /* dropped by firewall */
    }
    arp_cache_add(source_ip, eth_src);

    if (protocol == IPV4_PROTOCOL_ICMP) {
        uint32_t icmp_length = length - header_length;
        const uint8_t *icmp = payload + header_length;
        if (icmp_length < 4U) {
            return false;
        }
        if (icmp[0] == 8U) { /* echo request */
            uint8_t frame[ETH_HEADER_LEN + IPV4_HEADER_LEN + 256];
            uint32_t total_length = IPV4_HEADER_LEN + icmp_length;
            if (total_length > sizeof(frame) - ETH_HEADER_LEN) {
                return false;
            }
            uint8_t *packet = frame + ETH_HEADER_LEN;
            struct ethernet_header *eth = (struct ethernet_header *)frame;
            for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
                eth->dst[i] = eth_src[i];
                eth->src[i] = our_mac[i];
            }
            eth->ethertype = eth_be16(ETHERTYPE_IPV4);

            packet[0] = 0x45;
            packet[1] = 0x00;
            ipv4_write_be16(packet + 2, (uint16_t)total_length);
            ipv4_write_be16(packet + 4, ipv4_identification++);
            ipv4_write_be16(packet + 6, 0x00);
            packet[8] = 64U;
            packet[9] = IPV4_PROTOCOL_ICMP;
            packet[10] = 0x00;
            packet[11] = 0x00;
            ipv4_write_be32(packet + 12, our_ip);
            ipv4_write_be32(packet + 16, source_ip);
            packet[10] = (uint8_t)(ipv4_checksum(packet, IPV4_HEADER_LEN) >> 8);
            packet[11] = (uint8_t)ipv4_checksum(packet, IPV4_HEADER_LEN);

            uint8_t *reply_icmp = packet + IPV4_HEADER_LEN;
            reply_icmp[0] = 0U; /* echo reply */
            reply_icmp[1] = 0U;
            reply_icmp[2] = 0U;
            reply_icmp[3] = 0U;
            for (uint32_t i = 4U; i < icmp_length; i++) {
                reply_icmp[i] = icmp[i]; /* id, seq, data */
            }
            uint16_t checksum =
                icmp_checksum(reply_icmp, icmp_length);
            reply_icmp[2] = (uint8_t)(checksum >> 8);
            reply_icmp[3] = (uint8_t)checksum;

            if (!firewall_check(our_ip, source_ip, IPV4_PROTOCOL_ICMP)) {
                return true;
            }
            return netif_transmit(frame, ETH_HEADER_LEN + total_length);
        }
    }
    if (protocol == IPV4_PROTOCOL_UDP) {
        return udp_input(eth_src, payload, length);
    }
    return true;
}

bool ipv4_output(uint8_t protocol, uint32_t destination_ip,
                 const uint8_t *payload, uint32_t payload_length) {
    if (payload == 0 && payload_length != 0) {
        return false;
    }
    uint32_t total_length = IPV4_HEADER_LEN + payload_length;
    if (total_length > ETH_MAX_FRAME - ETH_HEADER_LEN) {
        return false;
    }
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);
    uint8_t next_hop_mac[ETH_ADDR_LEN];
    if (!arp_resolve(destination_ip, next_hop_mac)) {
        /* Probe first; the packet is dropped so the next attempt finds the
         * ARP entry after the peer replies. */
        arp_request(destination_ip);
        return false;
    }
    uint8_t frame[ETH_HEADER_LEN + IPV4_HEADER_LEN + 256];
    if (total_length > sizeof(frame) - ETH_HEADER_LEN) {
        return false;
    }
    uint8_t *packet = frame + ETH_HEADER_LEN;
    struct ethernet_header *eth = (struct ethernet_header *)frame;
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        eth->dst[i] = next_hop_mac[i];
        eth->src[i] = our_mac[i];
    }
    eth->ethertype = eth_be16(ETHERTYPE_IPV4);

    packet[0] = 0x45;
    packet[1] = 0x00;
    ipv4_write_be16(packet + 2, (uint16_t)total_length);
    ipv4_write_be16(packet + 4, ipv4_identification++);
    ipv4_write_be16(packet + 6, 0x00);
    packet[8] = 64U;
    packet[9] = protocol;
    packet[10] = 0x00;
    packet[11] = 0x00;
    ipv4_write_be32(packet + 12, our_ip);
    ipv4_write_be32(packet + 16, destination_ip);
    packet[10] = (uint8_t)(ipv4_checksum(packet, IPV4_HEADER_LEN) >> 8);
    packet[11] = (uint8_t)ipv4_checksum(packet, IPV4_HEADER_LEN);
    for (uint32_t i = 0; i < payload_length; i++) {
        packet[IPV4_HEADER_LEN + i] = payload[i];
    }
    if (!firewall_check(our_ip, destination_ip, protocol)) {
        return false;
    }
    return netif_transmit(frame, ETH_HEADER_LEN + total_length);
}

bool ipv4_send_icmp_echo(uint32_t destination_ip) {
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);
    if (!firewall_check(our_ip, destination_ip, IPV4_PROTOCOL_ICMP)) {
        return false;
    }
    uint8_t next_hop_mac[ETH_ADDR_LEN];
    if (!arp_resolve(destination_ip, next_hop_mac)) {
        arp_request(destination_ip);
        return false;
    }
    uint8_t frame[ETH_HEADER_LEN + IPV4_HEADER_LEN + 64];
    uint32_t total_length = IPV4_HEADER_LEN + 64;
    uint8_t *packet = frame + ETH_HEADER_LEN;
    struct ethernet_header *eth = (struct ethernet_header *)frame;
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++) {
        eth->dst[i] = next_hop_mac[i];
        eth->src[i] = our_mac[i];
    }
    eth->ethertype = eth_be16(ETHERTYPE_IPV4);

    packet[0] = 0x45;
    packet[1] = 0x00;
    ipv4_write_be16(packet + 2, (uint16_t)total_length);
    ipv4_write_be16(packet + 4, ipv4_identification++);
    ipv4_write_be16(packet + 6, 0x00);
    packet[8] = 64U;
    packet[9] = IPV4_PROTOCOL_ICMP;
    packet[10] = 0x00;
    packet[11] = 0x00;
    ipv4_write_be32(packet + 12, our_ip);
    ipv4_write_be32(packet + 16, destination_ip);
    packet[10] = (uint8_t)(ipv4_checksum(packet, IPV4_HEADER_LEN) >> 8);
    packet[11] = (uint8_t)ipv4_checksum(packet, IPV4_HEADER_LEN);

    uint8_t *icmp = packet + IPV4_HEADER_LEN;
    icmp[0] = 8U; /* echo request */
    icmp[1] = 0U;
    icmp[2] = 0U;
    icmp[3] = 0U;
    icmp[4] = 1U; /* id */
    icmp[5] = 0U;
    icmp[6] = 1U; /* sequence */
    icmp[7] = 0U;
    for (uint32_t i = 8U; i < 64U; i++) {
        icmp[i] = (uint8_t)('A' + (i - 8));
    }
    uint16_t checksum = icmp_checksum(icmp, 64U);
    icmp[2] = (uint8_t)(checksum >> 8);
    icmp[3] = (uint8_t)checksum;

    return netif_transmit(frame, ETH_HEADER_LEN + total_length);
}
