#ifndef ELOQUENCE_IPC_IPC_H
#define ELOQUENCE_IPC_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPC_MAX_SHM 8U
#define IPC_MAX_SEMAPHORES 8U
#define IPC_MAX_SIGNALS 16U
#define IPC_MAX_NAME 16U
#define IPC_MAX_MSG_QUEUES 4U
#define IPC_MAX_MSG_SIZE 256U
#define IPC_MAX_MSGS_PER_QUEUE 8U

/* Pipes. */
#define IPC_MAX_PIPES 8U
#define IPC_PIPE_BUFFER_SIZE 256U
#define IPC_PIPE_INVALID 0xffffffffffffffffULL

/* Pipe ends. */
#define IPC_PIPE_READ_END 0U
#define IPC_PIPE_WRITE_END 1U

/* Shared memory ids. */
#define IPC_SHM_INVALID 0xffffffffffffffffULL

/* Semaphore operations. */
#define IPC_SEM_WAIT 0U
#define IPC_SEM_POST 1U

/* Signal numbers. */
#define IPC_SIG_NONE 0U
#define IPC_SIG_TERM 1U
#define IPC_SIG_USR1 2U
#define IPC_SIG_USR2 3U

struct ipc_shm {
    bool in_use;
    char name[IPC_MAX_NAME];
    uint8_t data[4096];
    uint32_t size;
    uint32_t ref_count;
};

struct ipc_semaphore {
    bool in_use;
    char name[IPC_MAX_NAME];
    int32_t value;
};

/* Pending signal for a domain. */
struct ipc_signal_pending {
    bool in_use;
    uint64_t target_domain;
    uint8_t signal;
};

/* Message queue. */
struct ipc_msg_queue {
    bool in_use;
    char name[IPC_MAX_NAME];
    uint8_t msgs[IPC_MAX_MSGS_PER_QUEUE][IPC_MAX_MSG_SIZE];
    uint32_t msg_lens[IPC_MAX_MSGS_PER_QUEUE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
};

/* Pipe: bounded byte stream between two domains. */
struct ipc_pipe {
    bool in_use;
    char name[IPC_MAX_NAME];
    uint8_t data[IPC_PIPE_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t readers;
    uint32_t writers;
    uint64_t read_waitqueue;   /* processes blocked reading (pipe empty) */
    uint64_t write_waitqueue;  /* processes blocked writing (pipe full) */
};

bool ipc_init(void);

/* --- Shared memory --- */
uint64_t ipc_shm_get(const char *name, uint32_t size);
void *ipc_shmat(uint64_t shm_id);
bool ipc_shmdt(void *addr);
bool ipc_shmctl(uint64_t shm_id, uint32_t cmd);

/* --- Semaphores --- */
uint64_t ipc_sem_init(const char *name, int32_t initial_value);
bool ipc_sem_wait(uint64_t sem_id);
bool ipc_sem_post(uint64_t sem_id);
bool ipc_sem_destroy(uint64_t sem_id);

/* --- Signals --- */
bool ipc_signal_send(uint64_t target_domain, uint8_t signal);
uint8_t ipc_signal_recv(uint64_t *source_domain);

/* --- Message queues --- */
uint64_t ipc_mq_open(const char *name);
bool ipc_mq_send(uint64_t mq_id, const void *msg, uint32_t len);
bool ipc_mq_recv(uint64_t mq_id, void *msg, uint32_t max_len, uint32_t *out_len);
bool ipc_mq_close(uint64_t mq_id);

/* --- Pipes --- */
/* Create a named pipe. Returns a pipe id or IPC_PIPE_INVALID. */
uint64_t ipc_pipe_create(const char *name);
/* Write up to len bytes from data into the pipe. Returns bytes written
 * (0 when the pipe is full / would-block, or invalid). */
uint32_t ipc_pipe_write(uint64_t pipe_id, const uint8_t *data, uint32_t len);
/* Read up to max_len bytes from the pipe into buffer. Returns true and sets
 * out_len on success, false when empty/would-block or invalid. */
bool ipc_pipe_read(uint64_t pipe_id, uint8_t *buffer, uint32_t max_len,
                   uint32_t *out_len);
/* Close one end of the pipe (end = IPC_PIPE_READ_END or IPC_PIPE_WRITE_END).
 * Returns true on success, false when invalid. */
bool ipc_pipe_close(uint64_t pipe_id, uint32_t end);

#endif
