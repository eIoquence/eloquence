#ifndef ELOQUENCE_NET_ARP_H
#define ELOQUENCE_NET_ARP_H

#include <stdbool.h>
#include <stdint.h>

#include "netif.h"
#include "ethernet.h"

#define ARP_OP_REQUEST 1U
#define ARP_OP_REPLY 2U
#define ARP_HTYPE_ETHERNET 1U
#define ARP_PROTO_IPV4 0x0800U
#define ARP_CACHE_SIZE 4U

struct arp_entry {
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
    bool in_use;
};

/* Process a received ARP frame (Ethernet payload). Replies to ARP requests
 * addressed to us and caches the sender mapping. Returns true if the frame
 * was an ARP frame handled by this module. */
bool arp_input(const uint8_t *payload, uint32_t length);

/* Look up a MAC address for an IPv4 address in the ARP cache. */
bool arp_resolve(uint32_t ip, uint8_t mac[ETH_ADDR_LEN]);

/* Send an ARP request for the given IPv4 address (broadcast Ethernet). */
bool arp_request(uint32_t target_ip);

/* Add/replace a cache mapping. */
void arp_cache_add(uint32_t ip, const uint8_t mac[ETH_ADDR_LEN]);

#endif
