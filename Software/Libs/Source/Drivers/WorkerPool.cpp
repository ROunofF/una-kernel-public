/******************************************************************************
 * @file WorkerPool.cpp
 * @brief Worker pool implementation with CMSIS-RTOS v2
 *****************************************************************************/

#define LOG_MODULE_PRX      "WorkerPool"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "WorkerPool.hpp"
#include <cstring>
#include <new>

namespace OS {

WorkerPool& WorkerPool::GetInstance() {
    static WorkerPool inst;
    return inst;
}

bool WorkerPool::init(const WorkerPoolConfig& config) {
    LOG_INFO("Starting initialization\n");

    /* Check if already initialized */
    if (m_initialized) {
        LOG_WARNING("Already initialized, returning false\n");
        return false;
    }

    if (config.num_threads == 0 || config.num_threads > MAX_THREADS) {
        LOG_ERROR("Invalid thread count: %u (max %u)\n",
                 config.num_threads, MAX_THREADS);
        return false;
    }

    /* Create command queue using CMSIS-RTOS v2 */
    osMessageQueueAttr_t queue_attr = {
        .name = "worker_cmd_queue"
    };

    LOG_DEBUG("Creating command queue (length=%u)\n", config.command_queue_length);

    m_command_queue = osMessageQueueNew(
        config.command_queue_length,
        sizeof(Command),
        &queue_attr
    );

    if (!m_command_queue) {
        LOG_ERROR("Failed to create command queue\n");
        return false;
    }

    m_num_threads = config.num_threads;
    m_max_available_stack = 0;

    LOG_INFO("Creating %u worker threads\n", m_num_threads);

    /* Create worker threads */
    for (uint8_t i = 0; i < m_num_threads; i++) {
        const WorkerThreadConfig& thread_config = config.threads[i];

        /* Validate stack size (minimum 128 bytes, aligned to 4) */
        if (thread_config.stack_bytes < 128 ||
            (thread_config.stack_bytes % 4) != 0) {
            LOG_ERROR("Invalid stack size for thread %u: %u bytes\n",
                     i, thread_config.stack_bytes);
            return false;
        }

        /* Track maximum available stack */
        if (thread_config.stack_bytes > m_max_available_stack) {
            m_max_available_stack = thread_config.stack_bytes;
        }

        /* Generate thread name with stack size */
        static char task_names[MAX_THREADS][16];
        const char* priority_str = (thread_config.priority == WorkerPriority::HIGH)
                                   ? "h" : "n";

        /* Show stack in KB if >= 1024, otherwise in bytes */
        if (thread_config.stack_bytes >= 1024) {
            uint16_t kb = thread_config.stack_bytes / 1024;
            uint16_t remainder = (thread_config.stack_bytes % 1024) / 100;
            if (remainder > 0) {
                snprintf(task_names[i], sizeof(task_names[i]), "wrk_%s_%u.%uK",
                        priority_str, kb, remainder);
            } else {
                snprintf(task_names[i], sizeof(task_names[i]), "wrk_%s_%uK",
                        priority_str, kb);
            }
        } else {
            snprintf(task_names[i], sizeof(task_names[i]), "wrk_%s_%uB",
                    priority_str, thread_config.stack_bytes);
        }

        /* Map priority to CMSIS-RTOS priority */
        osPriority_t os_priority = (thread_config.priority == WorkerPriority::HIGH)
                                   ? osPriorityAboveNormal
                                   : osPriorityNormal;

        LOG_DEBUG("Creating thread '%s' (stack=%u bytes, priority=%s)\n",
                 task_names[i], thread_config.stack_bytes,
                 (thread_config.priority == WorkerPriority::HIGH) ? "HIGH" : "NORMAL");

        /* Store thread info BEFORE creating thread */
        m_threads[i].stack_bytes = thread_config.stack_bytes;
        m_threads[i].priority = thread_config.priority;
        m_threads[i].thread_index = i;
        m_threads[i].thread_id = nullptr;  // Not yet created

        /* Create thread attributes */
        osThreadAttr_t thread_attr = {
            .name = task_names[i],
            .stack_size = thread_config.stack_bytes,
            .priority = os_priority
        };

        /* Create thread */
        m_threads[i].thread_id = osThreadNew(
            workerThreadFunc,
            reinterpret_cast<void*>(static_cast<uintptr_t>(i)),
            &thread_attr
        );

        if (!m_threads[i].thread_id) {
            LOG_ERROR("Failed to create thread %u\n", i);
            return false;
        }

        LOG_DEBUG("Thread %u created successfully\n", i);
    }

    /* Mark as initialized */
    m_initialized = true;

    LOG_INFO("Initialization complete (max_stack=%u bytes)\n", m_max_available_stack);

    return true;
}

WorkerHandle_t WorkerPool::createWorker(const char* name,
                                        uint32_t period_ms,
                                        WorkerMode mode,
                                        WorkerPriority priority,
                                        uint16_t min_stack_bytes,
                                        WorkerCallback_t callback) {
    LOG_DEBUG("name='%s', period=%lu, mode=%s, priority=%s, stack=%u\n",
             name ? name : "unnamed",
             period_ms,
             (mode == WorkerMode::PERIODIC) ? "PERIODIC" : "ONE_SHOT",
             (priority == WorkerPriority::HIGH) ? "HIGH" : "NORMAL",
             min_stack_bytes);

    /* Check if pool is initialized */
    if (!m_initialized) {
        LOG_ERROR("Pool not initialized\n");
        return nullptr;
    }

    if (m_worker_count >= MAX_WORKERS || !callback) {
        LOG_ERROR("Max workers reached (%u) or invalid callback\n",
                 m_worker_count);
        return nullptr;
    }

    /* Check if requested stack exceeds maximum available */
    if (min_stack_bytes > m_max_available_stack) {
        LOG_ERROR("Requested stack %u exceeds max available %u\n",
                 min_stack_bytes, m_max_available_stack);
        return nullptr;
    }

    /* Allocate worker control block */
    WorkerControlBlock* worker = new (std::nothrow) WorkerControlBlock();
    if (!worker) {
        LOG_ERROR("Failed to allocate worker control block\n");
        return nullptr;
    }

    /* Initialize worker */
    strncpy(worker->name, name ? name : "unnamed", sizeof(worker->name) - 1);
    worker->name[sizeof(worker->name) - 1] = '\0';

    worker->callback_type = CallbackType::FUNCTION;
    worker->callback.function = callback;

    worker->context = nullptr;
    worker->period_ms = period_ms;
    worker->next_execution = 0;
    worker->mode = mode;
    worker->priority = priority;
    worker->min_stack_bytes = min_stack_bytes;
    worker->active = false;
    worker->pending_delete = false;

    /* Verify suitable thread exists */
    int8_t thread_idx = findBestThreadForWorker(worker);
    if (thread_idx < 0) {
        LOG_ERROR("No suitable thread found for worker '%s'\n", worker->name);
        delete worker;
        return nullptr;
    }

    LOG_DEBUG("Worker '%s' assigned to thread %d (stack=%u)\n",
             worker->name, thread_idx, m_threads[thread_idx].stack_bytes);

    m_workers[m_worker_count++] = worker;

    LOG_INFO("Worker '%s' created (total workers: %u)\n",
             worker->name, m_worker_count);

    return static_cast<WorkerHandle_t>(worker);
}

WorkerHandle_t WorkerPool::createWorker(const char* name,
                                        uint32_t period_ms,
                                        WorkerMode mode,
                                        WorkerPriority priority,
                                        uint16_t min_stack_bytes,
                                        WorkerCallbackBase* callback_obj) {
    LOG_DEBUG("(obj) name='%s', period=%lu, mode=%s, priority=%s, stack=%u\n",
             name ? name : "unnamed",
             period_ms,
             (mode == WorkerMode::PERIODIC) ? "PERIODIC" : "ONE_SHOT",
             (priority == WorkerPriority::HIGH) ? "HIGH" : "NORMAL",
             min_stack_bytes);

    /* Check if pool is initialized */
    if (!m_initialized) {
        LOG_ERROR("Pool not initialized\n");
        return nullptr;
    }

    if (m_worker_count >= MAX_WORKERS || !callback_obj) {
        LOG_ERROR("Max workers reached or invalid callback\n");
        return nullptr;
    }

    /* Check if requested stack exceeds maximum available */
    if (min_stack_bytes > m_max_available_stack) {
        LOG_ERROR("Requested stack %u exceeds max %u\n",
                 min_stack_bytes, m_max_available_stack);
        return nullptr;
    }

    WorkerControlBlock* worker = new (std::nothrow) WorkerControlBlock();
    if (!worker) {
        LOG_ERROR("Failed to allocate control block\n");
        return nullptr;
    }

    strncpy(worker->name, name ? name : "unnamed", sizeof(worker->name) - 1);
    worker->name[sizeof(worker->name) - 1] = '\0';

    worker->callback_type = CallbackType::OBJECT;
    worker->callback.object = callback_obj;

    worker->context = nullptr;
    worker->period_ms = period_ms;
    worker->next_execution = 0;
    worker->mode = mode;
    worker->priority = priority;
    worker->min_stack_bytes = min_stack_bytes;
    worker->active = false;
    worker->pending_delete = false;

    int8_t thread_idx = findBestThreadForWorker(worker);
    if (thread_idx < 0) {
        LOG_ERROR("No suitable thread for '%s'\n", worker->name);
        delete worker;
        return nullptr;
    }

    LOG_DEBUG("Worker '%s' assigned to thread %d\n",
             worker->name, thread_idx);

    m_workers[m_worker_count++] = worker;

    LOG_INFO("Worker '%s' created (total: %u)\n",
             worker->name, m_worker_count);

    return static_cast<WorkerHandle_t>(worker);
}

bool WorkerPool::startWorker(WorkerHandle_t handle, uint32_t delay_ms) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s' with delay=%lu ms\n", worker->name, delay_ms);

    Command cmd;
    cmd.type = CommandType::START;
    cmd.worker = worker;
    cmd.value = delay_ms;

    return postCommand(cmd);
}

bool WorkerPool::stopWorker(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s'\n", worker->name);

    Command cmd;
    cmd.type = CommandType::STOP;
    cmd.worker = worker;
    cmd.value = 0;

    return postCommand(cmd);
}

bool WorkerPool::resetWorker(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s'\n", worker->name);

    Command cmd;
    cmd.type = CommandType::RESET;
    cmd.worker = worker;
    cmd.value = 0;

    return postCommand(cmd);
}

bool WorkerPool::changePeriod(WorkerHandle_t handle, uint32_t new_period_ms) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s' from %lu to %lu ms\n",
             worker->name, worker->period_ms, new_period_ms);

    Command cmd;
    cmd.type = CommandType::CHANGE_PERIOD;
    cmd.worker = worker;
    cmd.value = new_period_ms;

    return postCommand(cmd);
}

bool WorkerPool::deleteWorker(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s'\n", worker->name);

    Command cmd;
    cmd.type = CommandType::DELETE;
    cmd.worker = worker;
    cmd.value = 0;

    return postCommand(cmd);
}

bool WorkerPool::forceExecute(WorkerHandle_t handle, ForceExecuteMode mode) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        LOG_ERROR("Invalid worker handle\n");
        return false;
    }

    LOG_DEBUG("'%s' (mode=%s)\n",
             worker->name,
             (mode == ForceExecuteMode::RESET_TIMER) ? "RESET" : "CONTINUE");

    Command cmd;
    cmd.type = CommandType::FORCE_EXECUTE;
    cmd.worker = worker;
    cmd.value = static_cast<uint32_t>(mode);

    return postCommand(cmd);
}

void WorkerPool::setContext(WorkerHandle_t handle, void* context) {
    WorkerControlBlock* worker = findWorker(handle);
    if (worker) {
        LOG_DEBUG("'%s' ctx=%p\n", worker->name, context);
        worker->context = context;
    }
}

void* WorkerPool::getContext(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    return worker ? worker->context : nullptr;
}

const char* WorkerPool::getName(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    return worker ? worker->name : "invalid";
}

bool WorkerPool::isActive(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    return worker ? worker->active : false;
}

uint32_t WorkerPool::getTimeUntilNext(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker || !worker->active) {
        return UINT32_MAX;
    }

    uint32_t current_time = osKernelGetTickCount();

    if (worker->next_execution <= current_time) {
        return 0;
    }

    return worker->next_execution - current_time;
}

uint16_t WorkerPool::getMinStackSize(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    return worker ? worker->min_stack_bytes : 0;
}

uint16_t WorkerPool::getActualStackSize(WorkerHandle_t handle) {
    WorkerControlBlock* worker = findWorker(handle);
    if (!worker) {
        return 0;
    }

    int8_t thread_idx = findBestThreadForWorker(worker);
    if (thread_idx < 0) {
        return 0;
    }

    return m_threads[thread_idx].stack_bytes;
}

bool WorkerPool::postCommand(const Command& cmd) {
    /* CMSIS-RTOS v2 automatically detects if called from ISR or task */
    osStatus_t status = osMessageQueuePut(
        m_command_queue,
        &cmd,
        0,
        100
    );

    if (status != osOK) {
        LOG_ERROR("Queue put failed (status=%d)\n", status);
        return false;
    }

    return true;
}

void WorkerPool::workerThreadFunc(void* argument) {
    uintptr_t thread_idx = reinterpret_cast<uintptr_t>(argument);
    WorkerPool& pool = WorkerPool::GetInstance();

    LOG_INFO("Thread %lu started\n", thread_idx);

    while (1) {
        uint32_t timeout_ms = pool.calculateTimeout(thread_idx);

        LOG_DEBUG("Thread[%lu]: timeout=%lu ms\n", thread_idx, timeout_ms);

        bool cmd_received = pool.processCommand(thread_idx, timeout_ms);

        if (!cmd_received) {
            LOG_DEBUG("Thread[%lu]: Timeout, executing ready workers\n", thread_idx);
            pool.executeReadyWorkers(thread_idx);
        }
    }
}

bool WorkerPool::processCommand(uint8_t thread_idx, uint32_t timeout_ms) {
    Command cmd;

    osStatus_t status = osMessageQueueGet(
        m_command_queue,
        &cmd,
        nullptr,
        timeout_ms
    );

    if (status != osOK) {
        return false;  /* Timeout or error */
    }

    LOG_DEBUG("Thread[%u]: cmd type=%d\n",
             thread_idx, static_cast<int>(cmd.type));

    if (cmd.worker) {
        int8_t best_thread = findBestThreadForWorker(cmd.worker);

        if (best_thread != static_cast<int8_t>(thread_idx)) {
            LOG_DEBUG("Thread[%u]: cmd for thread %d, re-queuing\n",
                     thread_idx, best_thread);
            osMessageQueuePut(m_command_queue, &cmd, 0, 0);
            return true;
        }
    }

    uint32_t current_time = osKernelGetTickCount();

    switch (cmd.type) {
        case CommandType::START:
            LOG_DEBUG("Thread[%u]: START '%s' at time=%lu\n",
                     thread_idx, cmd.worker->name, current_time + cmd.value);
            cmd.worker->active = true;
            cmd.worker->next_execution = current_time + cmd.value;
            break;

        case CommandType::STOP:
            LOG_DEBUG("Thread[%u]: STOP '%s'\n", thread_idx, cmd.worker->name);
            cmd.worker->active = false;
            break;

        case CommandType::RESET:
            if (cmd.worker->active) {
                LOG_DEBUG("Thread[%u]: RESET '%s'\n", thread_idx, cmd.worker->name);
                cmd.worker->next_execution = current_time + cmd.worker->period_ms;
            }
            break;

        case CommandType::CHANGE_PERIOD:
            LOG_DEBUG("Thread[%u]: CHANGE_PERIOD '%s' to %lu ms\n",
                     thread_idx, cmd.worker->name, cmd.value);
            cmd.worker->period_ms = cmd.value;
            if (cmd.worker->active) {
                cmd.worker->next_execution = current_time + cmd.value;
            }
            break;

        case CommandType::DELETE:
            LOG_DEBUG("Thread[%u]: DELETE '%s'\n", thread_idx, cmd.worker->name);
            cmd.worker->pending_delete = true;
            cmd.worker->active = false;

            for (uint8_t i = 0; i < m_worker_count; i++) {
                if (m_workers[i] == cmd.worker) {
                    delete m_workers[i];
                    for (uint8_t j = i; j < m_worker_count - 1; j++) {
                        m_workers[j] = m_workers[j + 1];
                    }
                    m_worker_count--;
                    LOG_INFO("Thread[%u]: Worker deleted (remaining: %u)\n",
                            thread_idx, m_worker_count);
                    break;
                }
            }
            break;

        case CommandType::FORCE_EXECUTE:
            LOG_DEBUG("Thread[%u]: FORCE_EXECUTE '%s'\n", thread_idx, cmd.worker->name);
            executeWorker(cmd.worker);

            if (cmd.worker->mode == WorkerMode::ONE_SHOT) {
                LOG_DEBUG("Thread[%u]: One-shot '%s' deactivated\n",
                         thread_idx, cmd.worker->name);
                cmd.worker->active = false;
            } else {
                ForceExecuteMode exec_mode = static_cast<ForceExecuteMode>(cmd.value);

                if (exec_mode == ForceExecuteMode::RESET_TIMER) {
                    if (cmd.worker->active) {
                        cmd.worker->next_execution = current_time + cmd.worker->period_ms;
                        LOG_DEBUG("Thread[%u]: Timer reset for '%s'\n",
                                 thread_idx, cmd.worker->name);
                    }
                }
            }
            break;
    }

    return true;
}

uint32_t WorkerPool::calculateTimeout(uint8_t thread_idx) {
    uint32_t current_time = osKernelGetTickCount();
    uint32_t min_timeout = osWaitForever;

    for (uint8_t i = 0; i < m_worker_count; i++) {
        WorkerControlBlock* worker = m_workers[i];

        if (!worker->active || worker->pending_delete) {
            continue;
        }

        if (!canThreadExecuteWorker(m_threads[thread_idx], worker)) {
            continue;
        }

        if (worker->next_execution > current_time) {
            uint32_t time_until = worker->next_execution - current_time;
            if (time_until < min_timeout) {
                min_timeout = time_until;
            }
        } else {
            return 0;
        }
    }

    return min_timeout;
}

void WorkerPool::executeReadyWorkers(uint8_t thread_idx) {
    uint32_t current_time = osKernelGetTickCount();

    for (uint8_t i = 0; i < m_worker_count; i++) {
        WorkerControlBlock* worker = m_workers[i];

        if (!worker->active || worker->pending_delete) {
            continue;
        }

        if (!canThreadExecuteWorker(m_threads[thread_idx], worker)) {
            continue;
        }

        if (current_time >= worker->next_execution) {
            LOG_DEBUG("Thread[%u]: Executing '%s'\n", thread_idx, worker->name);

            executeWorker(worker);

            if (worker->mode == WorkerMode::PERIODIC) {
                worker->next_execution = current_time + worker->period_ms;
                LOG_DEBUG("Thread[%u]: '%s' next at %lu\n",
                         thread_idx, worker->name, worker->next_execution);
            } else {
                worker->active = false;
                LOG_DEBUG("Thread[%u]: One-shot '%s' deactivated\n",
                         thread_idx, worker->name);
            }
        }
    }
}

void WorkerPool::executeWorker(WorkerControlBlock* worker) {
    WorkerHandle_t handle = static_cast<WorkerHandle_t>(worker);

    if (worker->callback_type == CallbackType::FUNCTION) {
        if (worker->callback.function) {
            worker->callback.function(handle);
        }
    } else {
        if (worker->callback.object) {
            worker->callback.object->onWorkerExecute(handle);
        }
    }
}

bool WorkerPool::canThreadExecuteWorker(const WorkerThread& thread,
                                        const WorkerControlBlock* worker) {
    /* Check priority match */
    if (thread.priority != worker->priority) {
        return false;
    }

    /* Check stack is sufficient (0 means any stack is ok) */
    if (worker->min_stack_bytes > 0 &&
        thread.stack_bytes < worker->min_stack_bytes) {
        return false;
    }

    return true;
}

int8_t WorkerPool::findBestThreadForWorker(const WorkerControlBlock* worker) {
    int8_t best_thread = -1;
    uint16_t smallest_suitable = UINT16_MAX;

    /* Find thread with matching priority and smallest sufficient stack */
    for (uint8_t i = 0; i < m_num_threads; i++) {
        if (canThreadExecuteWorker(m_threads[i], worker)) {
            if (best_thread < 0 || m_threads[i].stack_bytes < smallest_suitable) {
                best_thread = i;
                smallest_suitable = m_threads[i].stack_bytes;
            }
        }
    }

    return best_thread;
}

WorkerPool::WorkerControlBlock* WorkerPool::findWorker(WorkerHandle_t handle) {
    WorkerControlBlock* target = static_cast<WorkerControlBlock*>(handle);

    for (uint8_t i = 0; i < m_worker_count; i++) {
        if (m_workers[i] == target) {
            return m_workers[i];
        }
    }

    return nullptr;
}

} /* namespace OS */
