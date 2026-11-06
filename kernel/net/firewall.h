#ifndef ELOQUENCE_NET_FIREWALL_H
#define ELOQUENCE_NET_FIREWALL_H

#include <stdbool.h>
#include <stdint.h>

#define FIREWALL_MAX_RULES 16U

#define FIREWALL_ACTION_DENY 0U
#define FIREWALL_ACTION_ALLOW 1U

#define FIREWALL_PROTO_ANY 0U
#define FIREWALL_PROTO_ICMP 1U
#define FIREWALL_PROTO_TCP 6U
#define FIREWALL_PROTO_UDP 17U

struct firewall_rule {
    uint32_t src_ip;      /* 0 = any */
    uint32_t dst_ip;      /* 0 = any */
    uint8_t protocol;     /* 0 = any */
    uint8_t action;       /* FIREWALL_ACTION_* */
    bool in_use;
};

/* Returns true if the packet should be allowed, false if it should be
 * dropped. First matching rule wins; when no rule matches the default
 * policy (allow) applies. */
bool firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol);

/* Append a rule to the first free slot. Returns the rule index, or
 * (uint32_t)-1 when the table is full. */
uint32_t firewall_add_rule(uint32_t src_ip, uint32_t dst_ip,
                           uint8_t protocol, uint8_t action);

/* Remove the rule at index. Returns true if a rule was removed. */
bool firewall_remove_rule(uint32_t index);

/* Number of active rules. */
uint32_t firewall_rule_count(void);

/* Format one rule as a human-readable line ("ALLOW any->10.0.2.2 icmp").
 * Returns the number of bytes written (excluding NUL), or 0 when the index
 * is invalid. */
uint32_t firewall_format_rule(uint32_t index, char *buffer,
                              uint32_t capacity);

/* Default policy when no rule matches. */
bool firewall_default_allow(void);
void firewall_set_default_allow(bool allow);

#endif
