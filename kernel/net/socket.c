#include <stdbool.h>
#include <stdint.h>

#include "socket.h"
#include "netif.h"

static struct net_socket sockets[NET_MAX_SOCKETS];
static uint16_t next_port = 49152U; /* porturi efemere */

static bool port_in_use(uint32_t ip, uint16_t port, uint32_t protocol) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            continue;
        }
        if (sockets[i].local_port == port
            && (ip == 0 || sockets[i].local_ip == ip || sockets[i].local_ip == 0)
            && sockets[i].protocol == protocol) {
            return true;
        }
    }
    return false;
}

bool net_socket_init(void) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        sockets[i].in_use = false;
    }
    next_port = 49152U;
    return true;
}

uint32_t net_socket_alloc(uint32_t domain, uint32_t type, uint32_t protocol) {
    if (domain != NET_AF_INET) {
        return (uint32_t)-1;
    }
    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM) {
        return (uint32_t)-1;
    }
    if (protocol != 0U && protocol != 1U && protocol != 6U && protocol != 17U) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            sockets[i].in_use = true;
            sockets[i].protocol = protocol;
            sockets[i].type = (uint16_t)type;
            sockets[i].state = NET_SOCK_CLOSED;
            sockets[i].local_ip = 0;
            sockets[i].local_port = 0;
            sockets[i].peer_ip = 0;
            sockets[i].peer_port = 0;
            sockets[i].rx_length = 0;
            sockets[i].rx_ready = false;
            sockets[i].tx_length = 0;
            sockets[i].tx_pending = false;
            return i;
        }
    }
    return (uint32_t)-1;
}

bool net_socket_bind(uint32_t sock, uint32_t ip, uint16_t port) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return false;
    }
    if (sockets[sock].state != NET_SOCK_CLOSED) {
        return false;
    }
    if (port != 0 && port_in_use(ip, port, sockets[sock].protocol)) {
        return false;
    }
    if (port == 0) {
        /* Port efemer. */
        for (uint32_t try = 0; try < 1000U; try++) {
            uint16_t candidate = next_port++;
            if (candidate < 49152U) {
                candidate = 49152U;
            }
            if (candidate > 65535U) {
                candidate = 49152U;
            }
            if (!port_in_use(ip, candidate, sockets[sock].protocol)) {
                port = candidate;
                break;
            }
        }
        if (port == 0) {
            return false;
        }
    }
    sockets[sock].local_ip = ip;
    sockets[sock].local_port = port;
    return true;
}

bool net_socket_connect(uint32_t sock, uint32_t ip, uint16_t port) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return false;
    }
    if (ip == 0 || port == 0) {
        return false;
    }
    if (sockets[sock].protocol == NET_SOCK_STREAM) {
        /* TCP: marchează ca SYN_SENT; handshake-ul e făcut la trimitere. */
        sockets[sock].state = NET_SOCK_SYN_SENT;
    } else {
        sockets[sock].state = NET_SOCK_ESTABLISHED;
    }
    sockets[sock].peer_ip = ip;
    sockets[sock].peer_port = port;
    return true;
}

bool net_socket_listen(uint32_t sock, uint32_t backlog) {
    (void)backlog;
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return false;
    }
    if (sockets[sock].protocol != 6U || sockets[sock].type != NET_SOCK_STREAM) {
        return false;
    }
    if (sockets[sock].state != NET_SOCK_CLOSED || sockets[sock].local_port == 0) {
        return false;
    }
    sockets[sock].state = NET_SOCK_LISTEN;
    return true;
}

uint32_t net_socket_accept(uint32_t sock) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return (uint32_t)-1;
    }
    if (sockets[sock].state != NET_SOCK_LISTEN) {
        return (uint32_t)-1;
    }
    /* Pentru acum acceptăm orice socket în stare SYN_RECEIVED asociat. */
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use || i == sock) {
            continue;
        }
        if (sockets[i].state == NET_SOCK_SYN_RECEIVED
            && sockets[i].local_port == sockets[sock].local_port) {
            sockets[i].state = NET_SOCK_ESTABLISHED;
            return i;
        }
    }
    return (uint32_t)-1;
}

uint32_t net_socket_send(uint32_t sock, const void *data, uint32_t length) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return (uint32_t)-1;
    }
    if (data == 0 || length == 0 || length > NET_MAX_PACKET) {
        return (uint32_t)-1;
    }
    if (sockets[sock].tx_pending) {
        return (uint32_t)-1; /* încă o operațiune TX în curs */
    }
    for (uint32_t i = 0; i < length; i++) {
        sockets[sock].tx_buffer[i] = ((const uint8_t *)data)[i];
    }
    sockets[sock].tx_length = length;
    sockets[sock].tx_pending = true;
    return length;
}

uint32_t net_socket_recv(uint32_t sock, void *data, uint32_t length) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return (uint32_t)-1;
    }
    if (data == 0 || length == 0) {
        return (uint32_t)-1;
    }
    if (!sockets[sock].rx_ready) {
        return 0U; /* nicio dată disponibilă */
    }
    uint32_t copy = sockets[sock].rx_length;
    if (copy > length) {
        copy = length;
    }
    for (uint32_t i = 0; i < copy; i++) {
        ((uint8_t *)data)[i] = sockets[sock].rx_buffer[i];
    }
    sockets[sock].rx_ready = false;
    sockets[sock].rx_length = 0;
    return copy;
}

bool net_socket_close(uint32_t sock) {
    if (sock >= NET_MAX_SOCKETS || !sockets[sock].in_use) {
        return false;
    }
    sockets[sock].in_use = false;
    sockets[sock].state = NET_SOCK_CLOSED;
    sockets[sock].local_ip = 0;
    sockets[sock].local_port = 0;
    sockets[sock].peer_ip = 0;
    sockets[sock].peer_port = 0;
    sockets[sock].rx_length = 0;
    sockets[sock].rx_ready = false;
    sockets[sock].tx_length = 0;
    sockets[sock].tx_pending = false;
    return true;
}

uint32_t net_socket_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (sockets[i].in_use) {
            count++;
        }
    }
    return count;
}

static uint32_t append_u16(uint16_t value, char *buffer, uint32_t pos,
                           uint32_t capacity) {
    char digits[6];
    uint32_t dlen = 0;
    if (value == 0) {
        digits[dlen++] = '0';
    } else {
        uint16_t v = value;
        while (v != 0) {
            digits[dlen++] = (char)('0' + v % 10U);
            v /= 10U;
        }
    }
    while (dlen != 0) {
        if (pos + 1 >= capacity) {
            return pos;
        }
        buffer[pos++] = digits[--dlen];
    }
    return pos;
}

static const char *sock_proto_text(uint32_t proto) {
    switch (proto) {
    case 0U:
        return "any";
    case 1U:
        return "icmp";
    case 6U:
        return "tcp";
    case 17U:
        return "udp";
    default:
        return "?";
    }
}

uint32_t net_socket_format(uint32_t index, char *buffer, uint32_t capacity) {
    if (index >= NET_MAX_SOCKETS || !sockets[index].in_use || buffer == 0
        || capacity == 0) {
        return 0;
    }
    uint32_t pos = 0;
    /* "idx type proto local_ip:local_port -> peer_ip:peer_port state" */
    char digits[12];
    uint32_t dlen = 0;
    uint32_t value = index;
    if (value == 0) {
        digits[dlen++] = '0';
    } else {
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
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    pos += append_u16(sockets[index].type, buffer, pos, capacity);
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    pos = 0; /* proto text direct */
    {
        const char *pt = sock_proto_text(sockets[index].protocol);
        while (*pt != '\0') {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = *pt++;
        }
    }
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    /* local ip:port */
    for (int32_t i = 3; i >= 0; i--) {
        uint32_t octet = (sockets[index].local_ip >> (i * 8)) & 0xffU;
        if (i != 3) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = '.';
        }
        uint32_t o = octet;
        uint32_t od = 0;
        if (o == 0) {
            digits[od++] = '0';
        } else {
            while (o != 0) {
                digits[od++] = (char)('0' + o % 10U);
                o /= 10U;
            }
        }
        while (od != 0) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = digits[--od];
        }
    }
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ':';
    pos = append_u16(sockets[index].local_port, buffer, pos, capacity);
    if (pos + 3 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    buffer[pos++] = '-';
    buffer[pos++] = '-';
    buffer[pos++] = '>';
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    for (int32_t i = 3; i >= 0; i--) {
        uint32_t octet = (sockets[index].peer_ip >> (i * 8)) & 0xffU;
        if (i != 3) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = '.';
        }
        uint32_t o = octet;
        uint32_t od = 0;
        if (o == 0) {
            digits[od++] = '0';
        } else {
            while (o != 0) {
                digits[od++] = (char)('0' + o % 10U);
                o /= 10U;
            }
        }
        while (od != 0) {
            if (pos + 1 >= capacity) {
                return pos;
            }
            buffer[pos++] = digits[--od];
        }
    }
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ':';
    pos = append_u16(sockets[index].peer_port, buffer, pos, capacity);
    if (pos + 1 >= capacity) {
        return pos;
    }
    buffer[pos++] = ' ';
    switch (sockets[index].state) {
    case NET_SOCK_CLOSED:
        pos = 0; /* write "closed" */
        {
            const char *st = "closed";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    case NET_SOCK_LISTEN:
        {
            const char *st = "listen";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    case NET_SOCK_SYN_SENT:
        {
            const char *st = "syn-sent";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    case NET_SOCK_SYN_RECEIVED:
        {
            const char *st = "syn-recv";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    case NET_SOCK_ESTABLISHED:
        {
            const char *st = "established";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    case NET_SOCK_CLOSE_WAIT:
        {
            const char *st = "close-wait";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    default:
        {
            const char *st = "?";
            while (*st != '\0') {
                if (pos + 1 >= capacity) {
                    return pos;
                }
                buffer[pos++] = *st++;
            }
        }
        break;
    }
    buffer[pos] = '\0';
    return pos;
}

struct net_socket *net_socket_find(uint32_t ip, uint16_t port,
                                   uint32_t protocol) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            continue;
        }
        if (sockets[i].local_port == port
            && sockets[i].protocol == protocol
            && (ip == 0 || sockets[i].local_ip == ip || sockets[i].local_ip == 0)) {
            return &sockets[i];
        }
    }
    return 0;
}

struct net_socket *net_socket_get(uint32_t index) {
    if (index >= NET_MAX_SOCKETS || !sockets[index].in_use) {
        return 0;
    }
    return &sockets[index];
}
