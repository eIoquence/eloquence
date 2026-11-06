#include <stdint.h>

#include "arp.h"
#include "ethernet.h"
#include "ipv4.h"
#include "netif.h"
#include "socket.h"
#include "udp.h"

#define UDP_HEADER_LEN 8U

static uint16_t udp_read_be16(const uint8_t *cursor) {
    return (uint16_t)((uint16_t)cursor[0] << 8 | cursor[1]);
}

static void udp_write_be16(uint8_t *cursor, uint16_t value) {
    cursor[0] = (uint8_t)(value >> 8);
    cursor[1] = (uint8_t)value;
}

static uint16_t udp_checksum_one(const uint8_t *data, uint32_t length) {
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

uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t *data, uint32_t length) {
    /* Pseudo-header: src(4) + dst(4) + zero(1) + protocol(1) + len(2). */
    uint8_t pseudo[12];
    pseudo[0] = (uint8_t)(src_ip >> 24);
    pseudo[1] = (uint8_t)(src_ip >> 16);
    pseudo[2] = (uint8_t)(src_ip >> 8);
    pseudo[3] = (uint8_t)src_ip;
    pseudo[4] = (uint8_t)(dst_ip >> 24);
    pseudo[5] = (uint8_t)(dst_ip >> 16);
    pseudo[6] = (uint8_t)(dst_ip >> 8);
    pseudo[7] = (uint8_t)dst_ip;
    pseudo[8] = 0x00;
    pseudo[9] = 17U; /* UDP */
    pseudo[10] = (uint8_t)(length >> 8);
    pseudo[11] = (uint8_t)length;

    uint32_t sum = 0;
    for (uint32_t i = 0; i < 12; i += 2) {
        sum += (uint32_t)((uint16_t)pseudo[i] << 8 | pseudo[i + 1]);
    }
    sum += udp_checksum_one(data, length);
    while (sum >> 16) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

bool udp_input(const uint8_t eth_src[ETH_ADDR_LEN], const uint8_t *payload,
               uint32_t length) {
    if (eth_src == 0 || payload == 0 || length < UDP_HEADER_LEN) {
        return false;
    }
    const uint8_t *udp = payload;
    uint16_t src_port = udp_read_be16(udp + 0);
    uint16_t dst_port = udp_read_be16(udp + 2);
    uint16_t dgram_len = udp_read_be16(udp + 4);

    if (dgram_len < UDP_HEADER_LEN || dgram_len > length) {
        return false;
    }
    uint32_t data_length = dgram_len - UDP_HEADER_LEN;
    const uint8_t *data = udp + UDP_HEADER_LEN;

    struct net_socket *sock = net_socket_find(0, dst_port, 17U);
    if (sock == 0 || sock->rx_ready) {
        return false;
    }
    if (data_length > NET_MAX_PACKET) {
        data_length = NET_MAX_PACKET;
    }
    for (uint32_t i = 0; i < data_length; i++) {
        sock->rx_buffer[i] = data[i];
    }
    sock->rx_length = data_length;
    sock->rx_ready = true;
    sock->peer_port = src_port;
    return true;
}

bool udp_output(uint32_t destination_ip, uint16_t destination_port,
                uint16_t source_port, const uint8_t *payload,
                uint32_t payload_length) {
    if (payload == 0 && payload_length != 0) {
        return false;
    }
    uint32_t dgram_len = UDP_HEADER_LEN + payload_length;
    if (dgram_len > NET_MAX_PACKET - UDP_HEADER_LEN) {
        return false;
    }
    uint8_t frame[NET_MAX_PACKET];
    uint8_t *udp = frame;

    udp_write_be16(udp + 0, source_port);
    udp_write_be16(udp + 2, destination_port);
    udp_write_be16(udp + 4, (uint16_t)dgram_len);
    udp_write_be16(udp + 6, 0x00); /* checksum va fi calculat mai jos */

    for (uint32_t i = 0; i < payload_length; i++) {
        udp[UDP_HEADER_LEN + i] = payload[i];
    }

    uint16_t checksum =
        udp_checksum(0, destination_ip, udp, dgram_len);
    udp_write_be16(udp + 6, checksum);

    return ipv4_output(17U, destination_ip, udp, dgram_len);
}
