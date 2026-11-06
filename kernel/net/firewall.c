#include <stdbool.h>
#include <stdint.h>

#include "firewall.h"
#include "netif.h"

static struct firewall_rule rules[FIREWALL_MAX_RULES];
static bool default_allow = true;

static bool rule_matches(const struct firewall_rule *rule,
                         uint32_t src_ip, uint32_t dst_ip,
                         uint8_t protocol) {
    if (!rule->in_use) {
        return false;
    }
    if (rule->src_ip != 0 && rule->src_ip != src_ip) {
        return false;
    }
    if (rule->dst_ip != 0 && rule->dst_ip != dst_ip) {
        return false;
    }
    if (rule->protocol != 0 && rule->protocol != protocol) {
        return false;
    }
    return true;
}

bool firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol) {
    for (uint32_t i = 0; i < FIREWALL_MAX_RULES; i++) {
        if (rule_matches(&rules[i], src_ip, dst_ip, protocol)) {
            return rules[i].action == FIREWALL_ACTION_ALLOW;
        }
    }
    return default_allow;
}

uint32_t firewall_add_rule(uint32_t src_ip, uint32_t dst_ip,
                           uint8_t protocol, uint8_t action) {
    if (action != FIREWALL_ACTION_ALLOW && action != FIREWALL_ACTION_DENY) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0; i < FIREWALL_MAX_RULES; i++) {
        if (!rules[i].in_use) {
            rules[i].in_use = true;
            rules[i].src_ip = src_ip;
            rules[i].dst_ip = dst_ip;
            rules[i].protocol = protocol;
            rules[i].action = action;
            return i;
        }
    }
    return (uint32_t)-1;
}

bool firewall_remove_rule(uint32_t index) {
    if (index >= FIREWALL_MAX_RULES || !rules[index].in_use) {
        return false;
    }
    rules[index].in_use = false;
    return true;
}

uint32_t firewall_rule_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < FIREWALL_MAX_RULES; i++) {
        if (rules[i].in_use) {
            count++;
        }
    }
    return count;
}

static uint32_t append_text(char *buffer, uint32_t pos, uint32_t capacity,
                            const char *text) {
    while (*text != '\0') {
        if (pos + 1 >= capacity) {
            return pos;
        }
        buffer[pos++] = *text++;
    }
    return pos;
}

static uint32_t ip_append(uint32_t ip, char *buffer, uint32_t pos,
                          uint32_t capacity) {
    for (int32_t i = 3; i >= 0; i--) {
        uint32_t octet = (ip >> (i * 8)) & 0xffU;
        if (i != 3) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = '.';
        }
        char digits[4];
        uint32_t dlen = 0;
        if (octet == 0) {
            digits[dlen++] = '0';
        } else {
            uint32_t value = octet;
            while (value != 0) {
                digits[dlen++] = (char)('0' + value % 10U);
                value /= 10U;
            }
        }
        while (dlen != 0) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = digits[--dlen];
        }
    }
    return pos;
}

static const char *proto_text(uint8_t protocol) {
    switch (protocol) {
    case FIREWALL_PROTO_ANY:
        return "any";
    case FIREWALL_PROTO_ICMP:
        return "icmp";
    case FIREWALL_PROTO_TCP:
        return "tcp";
    case FIREWALL_PROTO_UDP:
        return "udp";
    default:
        return "?";
    }
}

static const char *action_text(uint8_t action) {
    return action == FIREWALL_ACTION_ALLOW ? "ALLOW" : "DENY";
}

uint32_t firewall_format_rule(uint32_t index, char *buffer,
                              uint32_t capacity) {
    if (index >= FIREWALL_MAX_RULES || !rules[index].in_use
        || buffer == 0 || capacity == 0) {
        return 0;
    }
    uint32_t pos = 0;
    pos = append_text(buffer, pos, capacity, action_text(rules[index].action));
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    if (rules[index].src_ip == 0) {
        pos = append_text(buffer, pos, capacity, "any");
    } else {
        pos = ip_append(rules[index].src_ip, buffer, pos, capacity);
    }
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = '-';
    buffer[pos++] = '>';
    if (rules[index].dst_ip == 0) {
        pos = append_text(buffer, pos, capacity, "any");
    } else {
        pos = ip_append(rules[index].dst_ip, buffer, pos, capacity);
    }
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    pos = append_text(buffer, pos, capacity, proto_text(rules[index].protocol));
    buffer[pos] = '\0';
    return pos;
}

bool firewall_default_allow(void) {
    return default_allow;
}

void firewall_set_default_allow(bool allow) {
    default_allow = allow;
}
