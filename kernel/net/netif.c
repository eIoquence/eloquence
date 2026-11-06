#include "netif.h"

#include "arch/x86_64/serial.h"

static struct netif netif;

static bool (*transmit_hook)(const uint8_t *frame, uint32_t length) = 0;

bool netif_transmit(const uint8_t *frame, uint32_t length) {
    if (!netif.link_up || frame == 0 || length == 0
        || length > NET_MAX_PACKET || transmit_hook == 0) {
        return false;
    }
    if (!transmit_hook(frame, length)) {
        return false;
    }
    netif.transmitted++;
    return true;
}

bool netif_input(const uint8_t *frame, uint32_t length) {
    if (frame == 0 || length == 0 || length > NET_MAX_PACKET
        || netif.count >= NET_RECEIVE_QUEUE) {
        return false;
    }
    struct net_packet *packet = &netif.packets[netif.tail];
    if (packet->in_use) {
        return false;
    }
    for (uint32_t i = 0; i < length; i++) {
        packet->data[i] = frame[i];
    }
    packet->length = length;
    packet->in_use = true;
    netif.tail = (netif.tail + 1U) % NET_RECEIVE_QUEUE;
    netif.count++;
    netif.received++;
    return true;
}

bool netif_receive_next(uint8_t *buffer, uint32_t capacity,
                        uint32_t *length) {
    if (buffer == 0 || length == 0 || netif.count == 0) {
        return false;
    }
    struct net_packet *packet = &netif.packets[netif.head];
    if (!packet->in_use) {
        return false;
    }
    uint32_t copy = packet->length;
    if (copy > capacity) {
        copy = capacity;
    }
    for (uint32_t i = 0; i < copy; i++) {
        buffer[i] = packet->data[i];
    }
    *length = copy;
    packet->in_use = false;
    netif.head = (netif.head + 1U) % NET_RECEIVE_QUEUE;
    netif.count--;
    return true;
}

uint32_t netif_receive_count(void) {
    return netif.count;
}

bool netif_set_link_up(bool up) {
    netif.link_up = up;
    return true;
}

bool netif_get_link_up(void) {
    return netif.link_up;
}

void netif_set_addresses(const uint8_t mac[6], uint32_t ipv4) {
    if (mac != 0) {
        for (uint32_t i = 0; i < 6; i++) {
            netif.mac[i] = mac[i];
        }
    }
    netif.ipv4 = ipv4;
}

void netif_get_addresses(uint8_t mac[6], uint32_t *ipv4) {
    if (mac != 0) {
        for (uint32_t i = 0; i < 6; i++) {
            mac[i] = netif.mac[i];
        }
    }
    if (ipv4 != 0) {
        *ipv4 = netif.ipv4;
    }
}

/* Driver registration of the transmit callback. */
bool netif_register_driver(bool (*transmit)(const uint8_t *frame,
                                            uint32_t length)) {
    if (transmit == 0 || transmit_hook != 0) {
        return false;
    }
    transmit_hook = transmit;
    return true;
}
