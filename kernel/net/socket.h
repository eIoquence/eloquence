#ifndef ELOQUENCE_NET_SOCKET_H
#define ELOQUENCE_NET_SOCKET_H

#include <stdbool.h>
#include <stdint.h>

#include "netif.h"

#define NET_MAX_SOCKETS 16U
#define NET_MAX_LISTEN_BACKLOG 4U
#define NET_MAX_PENDING_CONNECT 4U

/* Domenii și tipuri de socket. */
#define NET_AF_INET 2U
#define NET_SOCK_STREAM 1U   /* TCP */
#define NET_SOCK_DGRAM 2U    /* UDP */

/* Stări de socket. */
#define NET_SOCK_CLOSED 0U
#define NET_SOCK_LISTEN 1U
#define NET_SOCK_SYN_SENT 2U
#define NET_SOCK_SYN_RECEIVED 3U
#define NET_SOCK_ESTABLISHED 4U
#define NET_SOCK_CLOSE_WAIT 5U

struct net_socket {
    bool in_use;
    uint32_t protocol;      /* 0 = any, 1 = ICMP, 6 = TCP, 17 = UDP */
    uint16_t type;          /* NET_SOCK_STREAM / NET_SOCK_DGRAM */
    uint16_t state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t peer_ip;
    uint16_t peer_port;
    /* RX coadă simplă de pachete aflate aici. */
    uint8_t rx_buffer[NET_MAX_PACKET];
    uint32_t rx_length;
    bool rx_ready;
    /* TX coadă simplă. */
    uint8_t tx_buffer[NET_MAX_PACKET];
    uint32_t tx_length;
    bool tx_pending;
};

bool net_socket_init(void);

/* Creează un socket nou. Returnează indexul sau (uint32_t)-1 la eroare. */
uint32_t net_socket_alloc(uint32_t domain, uint32_t type, uint32_t protocol);

/* Leagă un socket de o adresă locală. */
bool net_socket_bind(uint32_t sock, uint32_t ip, uint16_t port);

/* Conectează un socket la o adresă remote. */
bool net_socket_connect(uint32_t sock, uint32_t ip, uint16_t port);

/* Pune un socket TCP în mod ascultare. */
bool net_socket_listen(uint32_t sock, uint32_t backlog);

/* Acceptă o conexiune TCP întâiale. Returnează noul index de socket sau (uint32_t)-1. */
uint32_t net_socket_accept(uint32_t sock);

/* Trimite date pe un socket. Returnează numărul de octeți trimisi sau (uint32_t)-1. */
uint32_t net_socket_send(uint32_t sock, const void *data, uint32_t length);

/* Primește date de pe un socket. Returnează numărul de octeți cititi sau (uint32_t)-1. */
uint32_t net_socket_recv(uint32_t sock, void *data, uint32_t length);

/* Închide un socket. */
bool net_socket_close(uint32_t sock);

/* Număr de socket-uri alocate. */
uint32_t net_socket_count(void);

/* Formatează un socket pentru afișare (debug). */
uint32_t net_socket_format(uint32_t index, char *buffer, uint32_t capacity);

/* Căutare socket pe cheie (local_ip, local_port, proto). */
struct net_socket *net_socket_find(uint32_t ip, uint16_t port, uint32_t protocol);

/* Căutare socket pe index. */
struct net_socket *net_socket_get(uint32_t index);

#endif
