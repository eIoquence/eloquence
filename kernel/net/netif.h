#ifndef ELOQUENCE_NET_NETIF_H
#define ELOQUENCE_NET_NETIF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_MAX_PACKET 1514U
#define NET_RECEIVE_QUEUE 8U

/* One received Ethernet frame waiting to be consumed by the stack/user. */
struct net_packet {
    uint8_t data[NET_MAX_PACKET];
    uint32_t length;
    bool in_use;
};

/* Network interface state: a small queue of received frames. */
struct netif {
    struct net_packet packets[NET_RECEIVE_QUEUE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t received;
    uint32_t transmitted;
    bool link_up;
    uint8_t mac[6];
    uint32_t ipv4;
};

/* Transmit one Ethernet frame through the underlying driver. */
bool netif_transmit(const uint8_t *frame, uint32_t length);

/* Deliver a received frame to the interface queue (called by the driver). */
bool netif_input(const uint8_t *frame, uint32_t length);

/* Poll the driver for newly received frames. */
void netif_poll(void);

/* Queued-packet access for the syscall layer. */
bool netif_receive_next(uint8_t *buffer, uint32_t capacity,
                        uint32_t *length);
uint32_t netif_receive_count(void);

/* Interface configuration. */
bool netif_set_link_up(bool up);
bool netif_get_link_up(void);
void netif_set_addresses(const uint8_t mac[6], uint32_t ipv4);
void netif_get_addresses(uint8_t mac[6], uint32_t *ipv4);

/* Driver registration of the transmit callback. */
bool netif_register_driver(bool (*transmit)(const uint8_t *frame,
                                            uint32_t length));

#endif
