#include <stdint.h>

#include "arp.h"
#include "ipv4.h"
#include "netif.h"
#include "socket.h"
#include "tcp.h"

static uint32_t tcp_iss = 0x1000U; /* initial send sequence */

static uint16_t tcp_read_be16(const uint8_t *cursor) {
    return (uint16_t)((uint16_t)cursor[0] << 8 | cursor[1]);
}

static uint32_t tcp_read_be32(const uint8_t *cursor) {
    return ((uint32_t)cursor[0] << 24) | ((uint32_t)cursor[1] << 16)
        | ((uint32_t)cursor[2] << 8) | (uint32_t)cursor[3];
}

static void tcp_write_be16(uint8_t *cursor, uint16_t value) {
    cursor[0] = (uint8_t)(value >> 8);
    cursor[1] = (uint8_t)value;
}

static void tcp_write_be32(uint8_t *cursor, uint32_t value) {
    cursor[0] = (uint8_t)(value >> 24);
    cursor[1] = (uint8_t)(value >> 16);
    cursor[2] = (uint8_t)(value >> 8);
    cursor[3] = (uint8_t)value;
}

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t *tcp_segment, uint32_t tcp_len) {
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
    pseudo[9] = 6U; /* TCP */
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)tcp_len;

    uint32_t sum = 0;
    for (uint32_t i = 0; i < 12; i += 2) {
        sum += (uint32_t)((uint16_t)pseudo[i] << 8 | pseudo[i + 1]);
    }
    for (uint32_t i = 0; i + 1 < tcp_len; i += 2) {
        sum += (uint32_t)((uint16_t)tcp_segment[i] << 8 | tcp_segment[i + 1]);
    }
    if (tcp_len & 1U) {
        sum += (uint32_t)tcp_segment[tcp_len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* Find a TCP listening socket by local port. */
static struct net_socket *tcp_find_listen(uint16_t local_port) {
    for (uint32_t i = 0; i < net_socket_count(); i++) {
        struct net_socket *s = net_socket_get(i);
        if (s == 0 || s->protocol != 6U || s->type != NET_SOCK_STREAM) {
            continue;
        }
        if (s->tcp_state == TCP_STATE_LISTEN && s->local_port == local_port) {
            return s;
        }
    }
    return 0;
}

/* Find an established TCP socket by 4-tuple. */
static struct net_socket *tcp_find_established(uint32_t local_ip,
                                               uint16_t local_port,
                                               uint32_t remote_ip,
                                               uint16_t remote_port) {
    for (uint32_t i = 0; i < net_socket_count(); i++) {
        struct net_socket *s = net_socket_get(i);
        if (s == 0 || s->protocol != 6U || s->type != NET_SOCK_STREAM) {
            continue;
        }
        if (s->tcp_state == TCP_STATE_ESTABLISHED
            && s->local_ip == local_ip
            && s->local_port == local_port
            && s->peer_ip == remote_ip
            && s->peer_port == remote_port) {
            return s;
        }
    }
    return 0;
}

static bool tcp_segment_input(const uint8_t *tcp_hdr, uint32_t tcp_len,
                              uint32_t src_ip, uint32_t dst_ip) {
    if (tcp_len < TCP_HEADER_LEN) {
        return false;
    }
    uint16_t src_port = tcp_read_be16(tcp_hdr + 0);
    uint16_t dst_port = tcp_read_be16(tcp_hdr + 2);
    uint32_t seq = tcp_read_be32(tcp_hdr + 4);
    uint32_t ack = tcp_read_be32(tcp_hdr + 8);
    uint8_t data_offset = (tcp_hdr[12] >> 4) & 0x0fU;
    uint8_t header_len = data_offset * 4U;
    uint8_t flags = tcp_hdr[13];

    if (header_len < TCP_HEADER_LEN || header_len > tcp_len) {
        return false;
    }
    const uint8_t *payload = tcp_hdr + header_len;
    uint32_t payload_len = tcp_len - header_len;

    /* Try established connection first (4-tuple match). */
    struct net_socket *sock = tcp_find_established(dst_ip, dst_port,
                                                   src_ip, src_port);
    if (sock != 0) {
        /* Handle ACK of our data. */
        if ((flags & TCP_FLAG_ACK) != 0) {
            sock->tcp_ack = ack;
        }
        /* Deliver payload. */
        if (payload_len != 0 && !sock->rx_ready) {
            if (payload_len > NET_MAX_PACKET) {
                payload_len = NET_MAX_PACKET;
            }
            for (uint32_t i = 0; i < payload_len; i++) {
                sock->rx_buffer[i] = payload[i];
            }
            sock->rx_length = payload_len;
            sock->rx_ready = true;
            sock->tcp_ack += payload_len;
        }
        /* Send ACK. */
        tcp_output(dst_ip, dst_port, src_ip, src_port,
                   TCP_FLAG_ACK, sock->tcp_seq, sock->tcp_ack, 0, 0);
        if ((flags & TCP_FLAG_FIN) != 0) {
            sock->tcp_state = TCP_STATE_CLOSE_WAIT;
        }
        return true;
    }

    /* Try listening socket (passive open). */
    sock = tcp_find_listen(dst_port);
    if (sock != 0 && sock->local_ip == 0) {
        if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) == 0) {
            /* Incoming SYN: reply with SYN-ACK. */
            sock->peer_ip = src_ip;
            sock->peer_port = src_port;
            sock->tcp_seq = tcp_iss++;
            sock->tcp_ack = seq + 1;
            sock->tcp_state = TCP_STATE_SYN_RECEIVED;
            return tcp_send_synack(sock);
        }
    }

    /* No matching socket: send RST. */
    (void)tcp_output(dst_ip, dst_port, src_ip, src_port,
                     TCP_FLAG_RST | TCP_FLAG_ACK, 0, ack + tcp_len, 0, 0);
    return true;
}

bool tcp_input(const uint8_t eth_src[ETH_ADDR_LEN], const uint8_t *payload,
               uint32_t length, uint32_t src_ip, uint32_t dst_ip) {
    if (eth_src == 0 || payload == 0 || length < TCP_HEADER_LEN) {
        return false;
    }
    (void)eth_src;
    return tcp_segment_input((const uint8_t *)payload, length, src_ip, dst_ip);
}

bool tcp_output(uint32_t local_ip, uint16_t local_port,
                uint32_t remote_ip, uint16_t remote_port,
                uint8_t flags, uint32_t seq, uint32_t ack,
                const uint8_t *payload, uint32_t payload_length) {
    (void)local_ip;
    if (payload == 0 && payload_length != 0) {
        return false;
    }
    uint8_t our_mac[ETH_ADDR_LEN];
    uint32_t our_ip;
    netif_get_addresses(our_mac, &our_ip);
    if (our_ip == 0) {
        return false;
    }
    uint32_t tcp_len = TCP_HEADER_LEN + payload_length;
    if (tcp_len > NET_MAX_PACKET) {
        return false;
    }
    uint8_t frame[NET_MAX_PACKET];
    uint8_t *tcp = frame;

    tcp_write_be16(tcp + 0, local_port);
    tcp_write_be16(tcp + 2, remote_port);
    tcp_write_be32(tcp + 4, seq);
    tcp_write_be32(tcp + 8, ack);
    tcp[12] = 0x50; /* data offset = 5 (20 bytes), no options */
    tcp[13] = flags;
    tcp_write_be16(tcp + 14, 0x4000); /* window = 16384 */
    tcp_write_be16(tcp + 16, 0x0000); /* checksum placeholder */
    tcp_write_be16(tcp + 18, 0x0000); /* urgent pointer */
    for (uint32_t i = 0; i < payload_length; i++) {
        tcp[TCP_HEADER_LEN + i] = payload[i];
    }

    uint16_t checksum =
        tcp_checksum(our_ip, remote_ip, tcp, tcp_len);
    tcp_write_be16(tcp + 16, checksum);

    return ipv4_output(6U, remote_ip, tcp, tcp_len);
}

bool tcp_send_syn(struct net_socket *sock) {
    if (sock == 0 || sock->protocol != 6U || sock->type != NET_SOCK_STREAM) {
        return false;
    }
    sock->tcp_seq = tcp_iss++;
    sock->tcp_state = TCP_STATE_SYN_SENT;
    /* Best-effort transmit; may fail if ARP is not yet resolved.
     * The SYN is retried on the next connect attempt. */
    (void)tcp_output(sock->local_ip, sock->local_port, sock->peer_ip,
                     sock->peer_port, TCP_FLAG_SYN, sock->tcp_seq, 0, 0, 0);
    return true;
}

bool tcp_send_synack(struct net_socket *sock) {
    if (sock == 0 || sock->protocol != 6U || sock->type != NET_SOCK_STREAM) {
        return false;
    }
    return tcp_output(sock->local_ip, sock->local_port, sock->peer_ip,
                      sock->peer_port, TCP_FLAG_SYN | TCP_FLAG_ACK,
                      sock->tcp_seq, sock->tcp_ack, 0, 0);
}

bool tcp_send_ack(struct net_socket *sock) {
    if (sock == 0 || sock->protocol != 6U || sock->type != NET_SOCK_STREAM) {
        return false;
    }
    return tcp_output(sock->local_ip, sock->local_port, sock->peer_ip,
                      sock->peer_port, TCP_FLAG_ACK, sock->tcp_seq,
                      sock->tcp_ack, 0, 0);
}

bool tcp_send_data(struct net_socket *sock, const uint8_t *data,
                   uint32_t length) {
    if (sock == 0 || sock->protocol != 6U || sock->type != NET_SOCK_STREAM
        || data == 0 || length == 0) {
        return false;
    }
    bool ok = tcp_output(sock->local_ip, sock->local_port, sock->peer_ip,
                         sock->peer_port, TCP_FLAG_ACK | TCP_FLAG_PSH,
                         sock->tcp_seq, sock->tcp_ack, data, length);
    if (ok) {
        sock->tcp_seq += length;
    }
    return ok;
}

bool tcp_send_fin(struct net_socket *sock) {
    if (sock == 0 || sock->protocol != 6U || sock->type != NET_SOCK_STREAM) {
        return false;
    }
    bool ok = tcp_output(sock->local_ip, sock->local_port, sock->peer_ip,
                         sock->peer_port, TCP_FLAG_FIN | TCP_FLAG_ACK,
                         sock->tcp_seq, sock->tcp_ack, 0, 0);
    if (ok) {
        sock->tcp_seq += 1;
        sock->tcp_state = TCP_STATE_CLOSE_WAIT;
    }
    return ok;
}
