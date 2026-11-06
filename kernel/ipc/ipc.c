#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/domain.h"
#include "ipc.h"

static struct ipc_shm shm_regions[IPC_MAX_SHM];
static struct ipc_semaphore semaphores[IPC_MAX_SEMAPHORES];
static struct ipc_signal_pending signals[IPC_MAX_SIGNALS];
static struct ipc_msg_queue msg_queues[IPC_MAX_MSG_QUEUES];

static bool ipc_initialized = false;

static bool text_eq(const char *a, const char *b);
static uint32_t text_copy(char *dst, const char *src, uint32_t capacity);

static int32_t shm_find_by_name(const char *name) {
    if (name == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < IPC_MAX_SHM; i++) {
        if (shm_regions[i].in_use && text_eq(shm_regions[i].name, name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t shm_alloc_slot(void) {
    for (uint32_t i = 0; i < IPC_MAX_SHM; i++) {
        if (!shm_regions[i].in_use) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sem_find_by_name(const char *name) {
    if (name == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < IPC_MAX_SEMAPHORES; i++) {
        if (semaphores[i].in_use && text_eq(semaphores[i].name, name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sem_alloc_slot(void) {
    for (uint32_t i = 0; i < IPC_MAX_SEMAPHORES; i++) {
        if (!semaphores[i].in_use) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t mq_find_by_name(const char *name) {
    if (name == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < IPC_MAX_MSG_QUEUES; i++) {
        if (msg_queues[i].in_use && text_eq(msg_queues[i].name, name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t mq_alloc_slot(void) {
    for (uint32_t i = 0; i < IPC_MAX_MSG_QUEUES; i++) {
        if (!msg_queues[i].in_use) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool text_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint32_t text_copy(char *dst, const char *src, uint32_t capacity) {
    uint32_t i = 0;
    while (src[i] != '\0' && i + 1 < capacity) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

bool ipc_init(void) {
    if (ipc_initialized) {
        return true;
    }
    for (uint32_t i = 0; i < IPC_MAX_SHM; i++) {
        shm_regions[i].in_use = false;
        shm_regions[i].size = 0;
        shm_regions[i].ref_count = 0;
    }
    for (uint32_t i = 0; i < IPC_MAX_SEMAPHORES; i++) {
        semaphores[i].in_use = false;
        semaphores[i].value = 0;
    }
    for (uint32_t i = 0; i < IPC_MAX_SIGNALS; i++) {
        signals[i].in_use = false;
        signals[i].target_domain = 0;
        signals[i].signal = IPC_SIG_NONE;
    }
    for (uint32_t i = 0; i < IPC_MAX_MSG_QUEUES; i++) {
        msg_queues[i].in_use = false;
        msg_queues[i].head = 0;
        msg_queues[i].tail = 0;
        msg_queues[i].count = 0;
    }
    ipc_initialized = true;
    return true;
}

/* --- Shared memory --- */

uint64_t ipc_shm_get(const char *name, uint32_t size) {
    if (!ipc_initialized || name == 0 || size == 0 || size > 4096U) {
        return IPC_SHM_INVALID;
    }
    int32_t existing = shm_find_by_name(name);
    if (existing >= 0) {
        shm_regions[existing].ref_count++;
        return (uint64_t)existing;
    }
    int32_t slot = shm_alloc_slot();
    if (slot < 0) {
        return IPC_SHM_INVALID;
    }
    shm_regions[slot].in_use = true;
    text_copy(shm_regions[slot].name, name, IPC_MAX_NAME);
    shm_regions[slot].size = size;
    shm_regions[slot].ref_count = 1;
    return (uint64_t)slot;
}

void *ipc_shmat(uint64_t shm_id) {
    if (!ipc_initialized || shm_id == IPC_SHM_INVALID
        || shm_id >= IPC_MAX_SHM) {
        return 0;
    }
    if (!shm_regions[shm_id].in_use) {
        return 0;
    }
    shm_regions[shm_id].ref_count++;
    return shm_regions[shm_id].data;
}

bool ipc_shmdt(void *addr) {
    if (!ipc_initialized || addr == 0) {
        return false;
    }
    uint8_t *byte_addr = (uint8_t *)addr;
    for (uint32_t i = 0; i < IPC_MAX_SHM; i++) {
        if (shm_regions[i].in_use
            && byte_addr >= shm_regions[i].data
            && byte_addr < shm_regions[i].data + shm_regions[i].size) {
            if (shm_regions[i].ref_count > 0) {
                shm_regions[i].ref_count--;
            }
            return true;
        }
    }
    return false;
}

bool ipc_shmctl(uint64_t shm_id, uint32_t cmd) {
    (void)cmd;
    if (!ipc_initialized || shm_id == IPC_SHM_INVALID
        || shm_id >= IPC_MAX_SHM) {
        return false;
    }
    if (!shm_regions[shm_id].in_use) {
        return false;
    }
    if (shm_regions[shm_id].ref_count > 0) {
        return false; /* still referenced */
    }
    shm_regions[shm_id].in_use = false;
    shm_regions[shm_id].size = 0;
    shm_regions[shm_id].ref_count = 0;
    return true;
}

/* --- Semaphores --- */

uint64_t ipc_sem_init(const char *name, int32_t initial_value) {
    if (!ipc_initialized || name == 0) {
        return IPC_SHM_INVALID;
    }
    int32_t existing = sem_find_by_name(name);
    if (existing >= 0) {
        return (uint64_t)existing;
    }
    int32_t slot = sem_alloc_slot();
    if (slot < 0) {
        return IPC_SHM_INVALID;
    }
    semaphores[slot].in_use = true;
    text_copy(semaphores[slot].name, name, IPC_MAX_NAME);
    semaphores[slot].value = initial_value;
    return (uint64_t)slot;
}

bool ipc_sem_wait(uint64_t sem_id) {
    if (!ipc_initialized || sem_id == IPC_SHM_INVALID
        || sem_id >= IPC_MAX_SEMAPHORES) {
        return false;
    }
    if (!semaphores[sem_id].in_use) {
        return false;
    }
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        return true;
    }
    /* Value is 0: would block. For now, return false (caller retries). */
    return false;
}

bool ipc_sem_post(uint64_t sem_id) {
    if (!ipc_initialized || sem_id == IPC_SHM_INVALID
        || sem_id >= IPC_MAX_SEMAPHORES) {
        return false;
    }
    if (!semaphores[sem_id].in_use) {
        return false;
    }
    semaphores[sem_id].value++;
    return true;
}

bool ipc_sem_destroy(uint64_t sem_id) {
    if (!ipc_initialized || sem_id == IPC_SHM_INVALID
        || sem_id >= IPC_MAX_SEMAPHORES) {
        return false;
    }
    if (!semaphores[sem_id].in_use) {
        return false;
    }
    semaphores[sem_id].in_use = false;
    semaphores[sem_id].value = 0;
    return true;
}

/* --- Signals --- */

bool ipc_signal_send(uint64_t target_domain, uint8_t signal) {
    if (!ipc_initialized || signal == IPC_SIG_NONE) {
        return false;
    }
    /* Find a free slot. */
    for (uint32_t i = 0; i < IPC_MAX_SIGNALS; i++) {
        if (!signals[i].in_use) {
            signals[i].in_use = true;
            signals[i].target_domain = target_domain;
            signals[i].signal = signal;
            return true;
        }
    }
    return false; /* signal queue full */
}

uint8_t ipc_signal_recv(uint64_t *source_domain) {
    if (!ipc_initialized || source_domain == 0) {
        return IPC_SIG_NONE;
    }
    /* Deliver the first signal for this domain. */
    for (uint32_t i = 0; i < IPC_MAX_SIGNALS; i++) {
        if (signals[i].in_use && signals[i].target_domain == *source_domain) {
            uint8_t sig = signals[i].signal;
            signals[i].in_use = false;
            signals[i].target_domain = 0;
            signals[i].signal = IPC_SIG_NONE;
            return sig;
        }
    }
    return IPC_SIG_NONE;
}

/* --- Message queues --- */

uint64_t ipc_mq_open(const char *name) {
    if (!ipc_initialized || name == 0) {
        return IPC_SHM_INVALID;
    }
    int32_t existing = mq_find_by_name(name);
    if (existing >= 0) {
        return (uint64_t)existing;
    }
    int32_t slot = mq_alloc_slot();
    if (slot < 0) {
        return IPC_SHM_INVALID;
    }
    msg_queues[slot].in_use = true;
    text_copy(msg_queues[slot].name, name, IPC_MAX_NAME);
    msg_queues[slot].head = 0;
    msg_queues[slot].tail = 0;
    msg_queues[slot].count = 0;
    return (uint64_t)slot;
}

bool ipc_mq_send(uint64_t mq_id, const void *msg, uint32_t len) {
    if (!ipc_initialized || mq_id == IPC_SHM_INVALID
        || mq_id >= IPC_MAX_MSG_QUEUES || msg == 0 || len == 0
        || len > IPC_MAX_MSG_SIZE) {
        return false;
    }
    if (!msg_queues[mq_id].in_use) {
        return false;
    }
    if (msg_queues[mq_id].count >= IPC_MAX_MSGS_PER_QUEUE) {
        return false; /* queue full */
    }
    uint32_t tail = msg_queues[mq_id].tail;
    for (uint32_t i = 0; i < len; i++) {
        msg_queues[mq_id].msgs[tail][i] = ((const uint8_t *)msg)[i];
    }
    msg_queues[mq_id].msg_lens[tail] = len;
    msg_queues[mq_id].tail = (tail + 1U) % IPC_MAX_MSGS_PER_QUEUE;
    msg_queues[mq_id].count++;
    return true;
}

bool ipc_mq_recv(uint64_t mq_id, void *msg, uint32_t max_len, uint32_t *out_len) {
    if (!ipc_initialized || mq_id == IPC_SHM_INVALID
        || mq_id >= IPC_MAX_MSG_QUEUES || msg == 0 || out_len == 0) {
        return false;
    }
    if (!msg_queues[mq_id].in_use) {
        return false;
    }
    if (msg_queues[mq_id].count == 0) {
        return false; /* queue empty */
    }
    uint32_t head = msg_queues[mq_id].head;
    uint32_t len = msg_queues[mq_id].msg_lens[head];
    if (len > max_len) {
        len = max_len;
    }
    for (uint32_t i = 0; i < len; i++) {
        ((uint8_t *)msg)[i] = msg_queues[mq_id].msgs[head][i];
    }
    *out_len = len;
    msg_queues[mq_id].head = (head + 1U) % IPC_MAX_MSGS_PER_QUEUE;
    msg_queues[mq_id].count--;
    return true;
}

bool ipc_mq_close(uint64_t mq_id) {
    if (!ipc_initialized || mq_id == IPC_SHM_INVALID
        || mq_id >= IPC_MAX_MSG_QUEUES) {
        return false;
    }
    if (!msg_queues[mq_id].in_use) {
        return false;
    }
    msg_queues[mq_id].in_use = false;
    msg_queues[mq_id].head = 0;
    msg_queues[mq_id].tail = 0;
    msg_queues[mq_id].count = 0;
    return true;
}
