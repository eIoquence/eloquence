#include <stdbool.h>
#include <stdint.h>

#include "capability/mmio_capability.h"
#include "domain/domain.h"
#include "drivers/virtio/virtio_net.h"
#include "net/firewall.h"
#include "net/ipv4.h"
#include "net/netif.h"
#include "net/socket.h"
#include "net/tcp.h"
#include "ipc/ipc.h"
#include "net/udp.h"
#include "drivers/video/fb.h"
#include "drivers/input/ps2.h"
#include "drivers/input/input_queue.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "serial.h"
#include "timer.h"
#include "system_info.h"
#include "syscall.h"
#include "eloquence/syscall_abi.h"
#include "user_mode.h"

static bool decode_handle(uint64_t packed,
                          struct mmio_capability_handle *handle) {
    if ((packed & 0x00000000ffffff00ULL) != 0 || handle == 0) {
        return false;
    }

    *handle = (struct mmio_capability_handle){
        .generation = (uint32_t)(packed >> 32),
        .slot = (uint8_t)packed
    };
    return true;
}

static uint64_t pack_result(uint32_t result,
                            uint32_t value) {
    return ((uint64_t)result << 32) | value;
}

/* Network receive staging buffer for the register-bound syscall interface. */
static uint8_t net_receive_buffer[NET_MAX_PACKET];
static uint32_t net_receive_length = 0;
/* Network send staging buffer (mirrors FS_WRITE staging). */
static uint8_t net_send_buffer[NET_MAX_PACKET];
static uint32_t net_send_length = 0;
static uint32_t net_send_sock = 0;

uint64_t x86_syscall_dispatch(const struct x86_syscall_frame *frame) {
    if (frame == 0 || kernel_domain_current_id() == KERNEL_DOMAIN_ID) {
        return pack_result(MMIO_CAPABILITY_ACCESS_DENIED, 0);
    }

    switch (frame->rax) {
        case ELOQUENCE_SYSCALL_EXIT:
            x86_user_mode_set_exit_status(frame->rdi);
            return ELOQUENCE_SYSCALL_EXIT_SENTINEL;
        case ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR:
            return pack_result(
                serial_write_user_char(frame->rdi)
                    ? ELOQUENCE_SYSCALL_RESULT_OK
                    : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                0);
        case ELOQUENCE_SYSCALL_CONSOLE_READ_CHAR: {
            char value;
            if (serial_try_read_char(&value)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                                   (uint8_t)value);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
        }
        case ELOQUENCE_SYSCALL_CONSOLE_CLEAR:
            serial_write("\033[2J\033[H");
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        case ELOQUENCE_SYSCALL_UPTIME_TICKS:
            return x86_timer_ticks();
        case ELOQUENCE_SYSCALL_SYSTEM_REPORT:
            return pack_result(kernel_system_report(frame->rdi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_LIST:
            return pack_result(kernel_system_file_list(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_LIST_CWD:
            return pack_result(kernel_system_file_list_current()
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_CHDIR:
            return pack_result(kernel_system_directory_change(frame->rdi,
                                                               frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_CAT:
            return pack_result(kernel_system_file_cat(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_READ_CHUNK: {
            uint32_t data = 0;
            uint32_t result = kernel_system_file_read_chunk(
                frame->rdi, frame->rsi, frame->rdx, &data);
            return pack_result(result, data);
        }
        case ELOQUENCE_SYSCALL_FS_STAT:
            return pack_result(kernel_system_file_stat(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_TOUCH:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_file_create(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_DELETE:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_file_delete(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_WRITE_BEGIN:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_file_write_begin(frame->rdi,
                                                               frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_WRITE_DATA:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_file_write_data(frame->rdi,
                                                              frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_WRITE_COMMIT:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_file_write_commit()
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_MKDIR:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_FS_WRITE_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_system_directory_create(frame->rdi,
                                                               frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_WRITE_ABORT:
            kernel_system_file_write_abort();
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        case ELOQUENCE_SYSCALL_PROCESS_LIST:
            return pack_result(kernel_system_process_list()
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_PROCESS_KILL:
            if (frame->rdi == kernel_domain_current_id()) {
                x86_user_mode_set_exit_status(137);
                return ELOQUENCE_SYSCALL_EXIT_SENTINEL;
            }
            if (!kernel_scheduler_kill(frame->rdi)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        case ELOQUENCE_SYSCALL_PROCESS_EXEC:
            return pack_result(kernel_system_process_exec(frame->rdi,
                                                           frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_PROCESS_SPAWN: {
            uint64_t process_id = kernel_system_process_spawn(frame->rdi,
                                                              frame->rsi);
            return pack_result(process_id != 0
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               (uint32_t)process_id);
        }
        case ELOQUENCE_SYSCALL_FS_COPY_BEGIN:
            return pack_result(kernel_system_file_copy_begin(frame->rdi,
                                                               frame->rsi,
                                                               frame->rdx)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_COPY_COMMIT:
            return pack_result(kernel_system_file_copy_commit(frame->rdi,
                                                              frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_PROCESS_WAIT: {
            struct kernel_process_info process;
            if (!kernel_process_get(frame->rdi, &process)
                || process.parent_id != kernel_domain_current_id()) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            if (process.state != KERNEL_PROCESS_EXITED
                && process.state != KERNEL_PROCESS_FAULTED) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)process.exit_status);
        }
        case ELOQUENCE_SYSCALL_CAP_QUERY:
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               kernel_domain_caps(kernel_domain_current_id()));
        case ELOQUENCE_SYSCALL_CAP_GRANT: {
            uint64_t current = kernel_domain_current_id();
            if (!kernel_domain_has(current, KERNEL_CAP_ADMIN_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            (void)kernel_domain_grant(current, (uint32_t)frame->rsi);
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_CAP_REVOKE: {
            uint64_t current = kernel_domain_current_id();
            if (!kernel_domain_has(current, KERNEL_CAP_ADMIN_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            (void)kernel_domain_revoke(current, (uint32_t)frame->rsi);
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_FS_RAM_CONTROL: {
            uint64_t operation = frame->rdi;
            if (operation == ELOQUENCE_RAM_OP_TOUCH
                || operation == ELOQUENCE_RAM_OP_DELETE
                || operation == ELOQUENCE_RAM_OP_WRITE_BEGIN
                || operation == ELOQUENCE_RAM_OP_WRITE_CHAR
                || operation == ELOQUENCE_RAM_OP_WRITE_COMMIT) {
                if (!kernel_domain_has(kernel_domain_current_id(),
                                       KERNEL_CAP_FS_WRITE_BIT)) {
                    return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
                }
            }
            return pack_result(kernel_system_ramfs_control(
                                    operation, frame->rsi, frame->rdx)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        }
        case ELOQUENCE_SYSCALL_NET_STATUS: {
            if (!netif_get_link_up()) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            uint32_t ipv4 = 0;
            netif_get_addresses(0, &ipv4);
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, ipv4);
        }
        case ELOQUENCE_SYSCALL_NET_RECEIVE: {
            virtio_net_poll();
            uint32_t length = 0;
            if (netif_receive_next(net_receive_buffer,
                                   sizeof(net_receive_buffer), &length)) {
                net_receive_length = length;
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, length);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_PING: {
            uint32_t destination_ip = (uint32_t)frame->rdi;
            if (destination_ip == 0 || !netif_get_link_up()) {
                return pack_result(destination_ip == 0
                                       ? ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT
                                       : ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK,
                                   0);
            }
            return pack_result(ipv4_send_icmp_echo(destination_ip)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK,
                               0);
        }
        case ELOQUENCE_SYSCALL_FW_ADD: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_ADMIN_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            uint32_t src_ip = (uint32_t)frame->rdi;
            uint32_t dst_ip = (uint32_t)frame->rsi;
            uint8_t protocol = (uint8_t)((frame->rdx >> 8) & 0xffU);
            uint8_t action = (uint8_t)(frame->rdx & 0xffU);
            uint32_t index = firewall_add_rule(src_ip, dst_ip, protocol,
                                               action);
            if (index == (uint32_t)-1) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, index);
        }
        case ELOQUENCE_SYSCALL_FW_REMOVE: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_ADMIN_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            bool ok = firewall_remove_rule((uint32_t)frame->rdi);
            return pack_result(ok ? ELOQUENCE_SYSCALL_RESULT_OK
                                  : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        }
        case ELOQUENCE_SYSCALL_FW_LIST: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_ADMIN_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            uint32_t pos = 0;
            for (uint32_t i = 0; i < FIREWALL_MAX_RULES; i++) {
                char line[64];
                uint32_t len = firewall_format_rule(i, line, sizeof(line));
                if (len == 0 || pos + len + 1 > sizeof(net_receive_buffer)) {
                    continue;
                }
                for (uint32_t j = 0; j < len; j++) {
                    net_receive_buffer[pos++] = line[j];
                }
                net_receive_buffer[pos++] = '\n';
            }
            net_receive_buffer[pos] = '\0';
            net_receive_length = pos;
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               firewall_rule_count());
        }
        case ELOQUENCE_SYSCALL_NET_SOCKET: {
            uint32_t fd = net_socket_alloc(frame->rdi, frame->rsi,
                                           frame->rdx);
            if (fd == (uint32_t)-1) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, fd);
        }
        case ELOQUENCE_SYSCALL_NET_BIND: {
            uint32_t sock = frame->rdi;
            uint32_t ip = frame->rsi;
            uint16_t port = (uint16_t)frame->rdx;
            if (!net_socket_bind(sock, ip, port)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_CONNECT: {
            uint32_t sock = frame->rdi;
            uint32_t ip = frame->rsi;
            uint16_t port = (uint16_t)frame->rdx;
            struct net_socket *s = net_socket_get(sock);
            if (s == 0 || ip == 0 || port == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            if (s->protocol == 6U && s->type == NET_SOCK_STREAM) {
                s->peer_ip = ip;
                s->peer_port = port;
                if (!tcp_send_syn(s)) {
                    return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                       0);
                }
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
            }
            if (!net_socket_connect(sock, ip, port)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_LISTEN: {
            uint32_t sock = frame->rdi;
            uint32_t backlog = frame->rsi;
            struct net_socket *s = net_socket_get(sock);
            if (s == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            if (s->protocol == 6U && s->type == NET_SOCK_STREAM) {
                if (s->local_port == 0) {
                    return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                       0);
                }
                s->tcp_state = TCP_STATE_LISTEN;
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
            }
            if (!net_socket_listen(sock, backlog)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_ACCEPT: {
            uint32_t sock = frame->rdi;
            struct net_socket *ls = net_socket_get(sock);
            if (ls == 0 || ls->protocol != 6U || ls->type != NET_SOCK_STREAM
                || ls->tcp_state != TCP_STATE_LISTEN) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            /* Find a socket in SYN_RECEIVED state on this listen port. */
            for (uint32_t i = 0; i < net_socket_count(); i++) {
                struct net_socket *c = net_socket_get(i);
                if (c == 0 || c == ls || c->protocol != 6U
                    || c->type != NET_SOCK_STREAM
                    || c->tcp_state != TCP_STATE_SYN_RECEIVED
                    || c->local_port != ls->local_port) {
                    continue;
                }
                c->tcp_state = TCP_STATE_ESTABLISHED;
                if (!tcp_send_ack(c)) {
                    return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, i);
                }
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, i);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_SEND_BEGIN: {
            uint32_t sock = frame->rdi;
            uint32_t length = frame->rsi;
            if (sock >= NET_MAX_SOCKETS || !net_socket_get(sock)
                || length == 0 || length > NET_MAX_PACKET
                || net_send_length != 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            net_send_sock = sock;
            net_send_length = length;
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_NET_SEND_DATA: {
            uint64_t offset = frame->rdi;
            uint64_t packed = frame->rsi;
            if (net_send_length == 0 || offset >= net_send_length) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            uint32_t remaining = net_send_length - (uint32_t)offset;
            uint32_t copy = remaining < 4U ? remaining : 4U;
            for (uint32_t i = 0; i < copy; i++) {
                net_send_buffer[(uint32_t)offset + i]
                    = (uint8_t)(packed >> (i * 8));
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, copy);
        }
        case ELOQUENCE_SYSCALL_NET_SEND_COMMIT: {
            uint32_t sock = frame->rdi;
            if (net_send_length == 0 || sock != net_send_sock) {
                net_send_length = 0;
                net_send_sock = 0;
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            struct net_socket *s = net_socket_get(sock);
            bool ok = false;
            if (s != 0 && s->protocol == 17U) {
                ok = udp_output(s->peer_ip, s->peer_port, s->local_port,
                                net_send_buffer, net_send_length);
            } else if (s != 0 && s->protocol == 6U
                       && s->type == NET_SOCK_STREAM) {
                ok = tcp_send_data(s, net_send_buffer, net_send_length);
            }
            net_send_length = 0;
            net_send_sock = 0;
            return pack_result(ok ? ELOQUENCE_SYSCALL_RESULT_OK
                                  : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        }
        case ELOQUENCE_SYSCALL_NET_RECV: {
            uint32_t sock = frame->rdi;
            uint32_t maxlen = frame->rsi;
            if (sock >= NET_MAX_SOCKETS || !net_socket_get(sock)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            struct net_socket *s = net_socket_get(sock);
            if (!s->rx_ready) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            uint32_t copy = s->rx_length;
            if (copy > maxlen) {
                copy = maxlen;
            }
            for (uint32_t i = 0; i < copy; i++) {
                net_receive_buffer[i] = s->rx_buffer[i];
            }
            net_receive_length = copy;
            s->rx_ready = false;
            s->rx_length = 0;
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, copy);
        }
        case ELOQUENCE_SYSCALL_NET_READ_BUFFER: {
            uint64_t offset = frame->rdi;
            uint64_t count = frame->rsi;
            if (net_receive_length == 0 || offset >= net_receive_length) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_END_OF_FILE, 0);
            }
            uint32_t remaining = net_receive_length - (uint32_t)offset;
            uint32_t copy = count < remaining ? (uint32_t)count : remaining;
            if (copy > 3U) {
                copy = 3U;
            }
            uint32_t data = 0;
            for (uint32_t i = 0; i < copy; i++) {
                data |= (uint32_t)net_receive_buffer[(uint32_t)offset + i]
                    << (i * 8);
            }
            data |= copy << 30;
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, data);
        }
        case ELOQUENCE_SYSCALL_NET_CLOSE: {
            uint32_t sock = frame->rdi;
            if (!net_socket_close(sock)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SHM_GET: {
            char name[IPC_MAX_NAME];
            uint64_t packed = frame->rdi;
            for (uint32_t i = 0; i < 8 && packed != 0; i++) {
                name[i] = (char)(packed & 0xffU);
                packed >>= 8;
            }
            name[7] = '\0';
            uint32_t size = (uint32_t)frame->rsi;
            uint64_t id = ipc_shm_get(name, size);
            if (id == IPC_SHM_INVALID) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, id);
        }
        case ELOQUENCE_SYSCALL_IPC_SHM_AT: {
            uint64_t id = frame->rdi;
            void *addr = ipc_shmat(id);
            if (addr == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint64_t)(uintptr_t)addr);
        }
        case ELOQUENCE_SYSCALL_IPC_SHM_DT: {
            void *addr = (void *)(uintptr_t)frame->rdi;
            if (!ipc_shmdt(addr)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SHM_CTL: {
            uint64_t id = frame->rdi;
            uint32_t cmd = (uint32_t)frame->rsi;
            if (!ipc_shmctl(id, cmd)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SEM_INIT: {
            char name[IPC_MAX_NAME];
            uint64_t packed = frame->rdi;
            for (uint32_t i = 0; i < 8 && packed != 0; i++) {
                name[i] = (char)(packed & 0xffU);
                packed >>= 8;
            }
            name[7] = '\0';
            int32_t value = (int32_t)frame->rsi;
            uint64_t id = ipc_sem_init(name, value);
            if (id == IPC_SHM_INVALID) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, id);
        }
        case ELOQUENCE_SYSCALL_IPC_SEM_WAIT: {
            uint64_t id = frame->rdi;
            if (!ipc_sem_wait(id)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SEM_POST: {
            uint64_t id = frame->rdi;
            if (!ipc_sem_post(id)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SEM_DESTROY: {
            uint64_t id = frame->rdi;
            if (!ipc_sem_destroy(id)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SIGNAL_SEND: {
            uint64_t target = frame->rdi;
            uint8_t sig = (uint8_t)frame->rsi;
            if (!ipc_signal_send(target, sig)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_SIGNAL_RECV: {
            uint64_t source = frame->rdi;
            uint8_t sig = ipc_signal_recv(&source);
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, sig);
        }
        case ELOQUENCE_SYSCALL_IPC_MQ_OPEN: {
            char name[IPC_MAX_NAME];
            uint64_t packed = frame->rdi;
            for (uint32_t i = 0; i < 8 && packed != 0; i++) {
                name[i] = (char)(packed & 0xffU);
                packed >>= 8;
            }
            name[7] = '\0';
            uint64_t id = ipc_mq_open(name);
            if (id == IPC_SHM_INVALID) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, id);
        }
        case ELOQUENCE_SYSCALL_IPC_MQ_SEND: {
            uint64_t id = frame->rdi;
            uint64_t packed_msg = frame->rsi;
            uint32_t len = (uint32_t)frame->rdx;
            if (len > IPC_MAX_MSG_SIZE) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            uint8_t msg[IPC_MAX_MSG_SIZE];
            for (uint32_t i = 0; i < len; i++) {
                msg[i] = (uint8_t)(packed_msg & 0xffU);
                packed_msg >>= 8;
            }
            if (!ipc_mq_send(id, msg, len)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_MQ_RECV: {
            uint64_t id = frame->rdi;
            void *buffer = (void *)(uintptr_t)frame->rsi;
            uint32_t max_len = (uint32_t)frame->rdx;
            uint32_t out_len = 0;
            if (!ipc_mq_recv(id, buffer, max_len, &out_len)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, out_len);
        }
        case ELOQUENCE_SYSCALL_IPC_MQ_CLOSE: {
            uint64_t id = frame->rdi;
            if (!ipc_mq_close(id)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_INFO: {
            /* Deprecated: writing through a userspace pointer from the kernel
             * is unsafe with separate user/kernel page tables. Use the
             * individual GFX_GET_* syscalls (83-88) instead. */
            return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT, 0);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_ADDR: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)(uintptr_t)info->address);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_WIDTH: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)info->width);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_HEIGHT: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)info->height);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_PITCH: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)info->pitch);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_BPP: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint32_t)info->bpp);
        }
        case ELOQUENCE_SYSCALL_GFX_GET_SIZE: {
            const struct framebuffer_info *info = fb_get_info();
            if (info == 0) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               info->framebuffer_size);
        }
        case ELOQUENCE_SYSCALL_GFX_DRAW_PIXEL: {
            uint64_t x = frame->rdi;
            uint64_t y = frame->rsi;
            uint32_t color = (uint32_t)frame->rdx;
            if (!fb_put_pixel(x, y, color)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_GFX_FILL_RECT: {
            uint64_t x = frame->rdi;
            uint64_t y = frame->rsi;
            uint64_t w = frame->rdx;
            uint64_t h = frame->r10;
            uint32_t color = (uint32_t)frame->r8;
            if (!fb_fill_rect(x, y, w, h, color)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_GFX_CLEAR: {
            uint32_t color = (uint32_t)frame->rdi;
            if (!fb_clear(color)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_GFX_FLIP: {
            if (!fb_flip()) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_INPUT_GET: {
            ps2_poll();
            struct input_event ev;
            if (!input_queue_pop(&ev)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               ((uint64_t)ev.type << 56) |
                               ((uint64_t)ev.key_code << 48) |
                               ((uint64_t)(uint32_t)ev.x << 32) |
                               (uint32_t)ev.y);
        }
        case ELOQUENCE_SYSCALL_INPUT_POLL: {
            ps2_poll();
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                               (uint64_t)input_queue_count());
        }
        case ELOQUENCE_SYSCALL_IPC_CREATE:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_ipc_create(frame->rdi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_IPC_SEND:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            return pack_result(kernel_ipc_send(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK,
                               0);
        case ELOQUENCE_SYSCALL_IPC_RECEIVE: {
            uint8_t byte = 0;
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            if (kernel_ipc_receive(frame->rdi, &byte)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, byte);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
        }
        case ELOQUENCE_SYSCALL_IPC_PIPE_CREATE: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            char name[IPC_MAX_NAME];
            uint64_t packed = frame->rdi;
            for (uint32_t i = 0; i < 8 && packed != 0; i++) {
                name[i] = (char)(packed & 0xffU);
                packed >>= 8;
            }
            name[7] = '\0';
            uint64_t id = ipc_pipe_create(name);
            if (id == IPC_PIPE_INVALID) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, id);
        }
        case ELOQUENCE_SYSCALL_IPC_PIPE_WRITE: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            uint64_t id = frame->rdi;
            uint64_t packed = frame->rsi;
            uint32_t len = (uint32_t)frame->rdx;
            uint8_t data[IPC_PIPE_BUFFER_SIZE];
            uint32_t to_copy = len < 8U ? len : 8U;
            for (uint32_t i = 0; i < to_copy; i++) {
                data[i] = (uint8_t)(packed & 0xffU);
                packed >>= 8;
            }
            uint32_t written = ipc_pipe_write(id, data, len);
            if (written == 0U) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, written);
        }
        case ELOQUENCE_SYSCALL_IPC_PIPE_READ: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            uint64_t id = frame->rdi;
            uint8_t *buffer = (uint8_t *)(uintptr_t)frame->rsi;
            uint32_t max_len = (uint32_t)frame->rdx;
            uint32_t out_len = 0;
            if (!ipc_pipe_read(id, buffer, max_len, &out_len)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, out_len);
        }
        case ELOQUENCE_SYSCALL_IPC_PIPE_CLOSE: {
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_IPC_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            uint64_t id = frame->rdi;
            uint32_t end = (uint32_t)frame->rsi;
            if (!ipc_pipe_close(id, end)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                                   0);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        }
        case ELOQUENCE_SYSCALL_SYSTEM_CONTROL:
            if (!kernel_domain_has(kernel_domain_current_id(),
                                   KERNEL_CAP_SYSTEM_CONTROL_BIT)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_DENIED, 0);
            }
            if (frame->rdi == 1) {
                __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            } else {
                __asm__ volatile ("cli");
                for (;;) {
                    __asm__ volatile ("hlt");
                }
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        default:
            break;
    }

    struct mmio_capability_handle handle;
    if (!decode_handle(frame->rdi, &handle)) {
        handle = (struct mmio_capability_handle){ .slot = UINT8_MAX };
    }
    switch (frame->rax) {
        case ELOQUENCE_SYSCALL_MMIO_READ32: {
            uint32_t value = 0;
            enum mmio_capability_result result = mmio_capability_read32(
                handle, frame->rsi, &value);
            return pack_result(result, value);
        }
        case ELOQUENCE_SYSCALL_MMIO_WRITE32:
            return mmio_capability_write32(handle, frame->rsi,
                                           (uint32_t)frame->rdx);
        case ELOQUENCE_SYSCALL_MMIO_REVOKE:
            return mmio_capability_revoke(handle);
        default:
            return pack_result(MMIO_CAPABILITY_INVALID_ARGUMENT, 0);
    }
}
