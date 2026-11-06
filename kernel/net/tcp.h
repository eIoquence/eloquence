#ifndef ELOQUENCE_NET_TCP_H
#define ELOQUENCE_NET_TCP_H

#include <stdbool.h>
#include <stdint.h>

#include "ethernet.h"
#include "socket.h"

/* TCP flags. */
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_FLAG_URG 0x20U

/* TCP socket states. */
#define TCP_STATE_CLOSED 0U
#define TCP_STATE_LISTEN 1U
#define TCP_STATE_SYN_SENT 2U
#define TCP_STATE_SYN_RECEIVED 3U
#define TCP_STATE_ESTABLISHED 4U
#define TCP_STATE_CLOSE_WAIT 5U

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

#define TCP_HEADER_LEN 20U

/* Process a received TCP segment (IPv4 payload). Delivers to the matching
 * socket. Returns true if the frame was TCP and handled. */
bool tcp_input(const uint8_t eth_src[ETH_ADDR_LEN], const uint8_t *payload,
               uint32_t length, uint32_t src_ip, uint32_t dst_ip);

/* Send a TCP segment. Builds the IP/TCP header and submits to the interface. */
bool tcp_output(uint32_t local_ip, uint16_t local_port,
                uint32_t remote_ip, uint16_t remote_port,
                uint8_t flags, uint32_t seq, uint32_t ack,
                const uint8_t *payload, uint32_t payload_length);

/* Send a SYN from a socket (active open). */
bool tcp_send_syn(struct net_socket *sock);

/* Send a SYN-ACK from a socket (passive open reply). */
bool tcp_send_synack(struct net_socket *sock);

/* Send an ACK from a socket. */
bool tcp_send_ack(struct net_socket *sock);

/* Send data from a socket. */
bool tcp_send_data(struct net_socket *sock, const uint8_t *data,
                   uint32_t length);

/* Close a TCP socket (send FIN). */
bool tcp_send_fin(struct net_socket *sock);

/* TCP checksum (with pseudo-header). */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t *tcp_segment, uint32_t tcp_len);

#endif
