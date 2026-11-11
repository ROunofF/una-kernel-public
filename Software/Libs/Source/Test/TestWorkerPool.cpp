/******************************************************************************
 * @file TestWorkerPool.cpp
 * @brief Worker pool test suite
 *****************************************************************************/

#define LOG_MODULE_PRX      "TestWorkerPool"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestWorkerPool.hpp"

#include "OS.hpp"
#include "WorkerPool.hpp"

/* ========================================================================== */
/* Test Helpers                                                               */
/* ========================================================================== */

static volatile uint32_t g_test_counter = 0;
static volatile uint32_t g_test_flag = 0;

static void resetTestCounters()
{
    g_test_counter = 0;
    g_test_flag = 0;
}

/* ========================================================================== */
/* Test 1: Basic Initialization                                              */
/* ========================================================================== */

static bool test_initialization()
{
    LOG_INFO("Test 1: Initialization\n");

    /* Test that pool is not initialized yet */
    if (OS::WorkerPool::GetInstance().isInitialized()) {
        LOG_ERROR("FAILED: Pool should not be initialized yet\n");
        return false;
    }

    /* Initialize pool */
    bool result = OS::WorkerPool::GetInstance().init(OS::DEFAULT_WORKER_POOL_CONFIG);
    if (!result) {
        LOG_ERROR("FAILED: Pool initialization failed\n");
        return false;
    }

    /* Test that pool is now initialized */
    if (!OS::WorkerPool::GetInstance().isInitialized()) {
        LOG_ERROR("FAILED: Pool should be initialized\n");
        return false;
    }

    /* Test that second init fails */
    result = OS::WorkerPool::GetInstance().init(OS::DEFAULT_WORKER_POOL_CONFIG);
    if (result) {
        LOG_ERROR("FAILED: Second initialization should fail\n");
        return false;
    }

    LOG_INFO("Test 1: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 2: Simple Periodic Worker                                            */
/* ========================================================================== */

static void periodicWorkerCallback(OS::WorkerHandle_t handle)
{
    g_test_counter++;
    LOG_DEBUG("Periodic worker executed, count=%lu\n", g_test_counter);
}

static bool test_periodic_worker()
{
    LOG_INFO("Test 2: Periodic Worker\n");

    resetTestCounters();

    /* Create periodic worker - execute every 500ms */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_periodic",
        500,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        periodicWorkerCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    if (OS::WorkerPool::GetInstance().isActive(worker)) {
        LOG_ERROR("FAILED: Worker should not be active yet\n");
        return false;
    }

    /* Start worker */
    bool result = OS::WorkerPool::GetInstance().startWorker(worker);
    if (!result) {
        LOG_ERROR("FAILED: Worker start failed\n");
        return false;
    }

    if (!OS::WorkerPool::GetInstance().isActive(worker)) {
        LOG_ERROR("FAILED: Worker should be active\n");
        return false;
    }

    /* Wait for 5 executions (2.5 seconds) */
    OS::Delay(2600);

    LOG_DEBUG("Counter after 2.6s: %lu (expected ~5)\n", g_test_counter);
    if (g_test_counter < 4 || g_test_counter > 6) {
        LOG_ERROR("FAILED: Counter=%lu, expected 4-6\n", g_test_counter);
        return false;
    }

    /* Stop worker */
    result = OS::WorkerPool::GetInstance().stopWorker(worker);
    if (!result) {
        LOG_ERROR("FAILED: Worker stop failed\n");
        return false;
    }

    if (OS::WorkerPool::GetInstance().isActive(worker)) {
        LOG_ERROR("FAILED: Worker should not be active after stop\n");
        return false;
    }

    uint32_t count_before = g_test_counter;
    OS::Delay(1000);

    /* Counter should not change after stop */
    if (g_test_counter != count_before) {
        LOG_ERROR("FAILED: Counter changed after stop (%lu -> %lu)\n",
                 count_before, g_test_counter);
        return false;
    }

    LOG_INFO("Test 2: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 3: One-Shot Worker                                                   */
/* ========================================================================== */

static void oneShotWorkerCallback(OS::WorkerHandle_t handle)
{
    g_test_flag = 1;
    LOG_DEBUG("One-shot worker executed\n");
}

static bool test_oneshot_worker()
{
    LOG_INFO("Test 3: One-Shot Worker\n");

    resetTestCounters();

    /* Create one-shot worker */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_oneshot",
        0,
        OS::WorkerMode::ONE_SHOT,
        OS::WorkerPriority::NORMAL,
        0,
        oneShotWorkerCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    /* Start worker with 200ms delay */
    bool result = OS::WorkerPool::GetInstance().startWorker(worker, 200);
    if (!result) {
        LOG_ERROR("FAILED: Worker start failed\n");
        return false;
    }

    if (!OS::WorkerPool::GetInstance().isActive(worker)) {
        LOG_ERROR("FAILED: Worker should be active\n");
        return false;
    }

    /* Flag should be 0 immediately */
    if (g_test_flag != 0) {
        LOG_ERROR("FAILED: Flag should be 0 before execution\n");
        return false;
    }

    /* Wait for execution */
    OS::Delay(300);

    /* Flag should be 1 after execution */
    if (g_test_flag != 1) {
        LOG_ERROR("FAILED: Flag should be 1 after execution\n");
        return false;
    }

    /* Worker should be inactive after one-shot execution */
    if (OS::WorkerPool::GetInstance().isActive(worker)) {
        LOG_ERROR("FAILED: One-shot worker should be inactive after execution\n");
        return false;
    }

    LOG_INFO("Test 3: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 4: Worker with Class Method                                          */
/* ========================================================================== */

class TestWorkerClass : public OS::WorkerCallbackBase
{
public:
    TestWorkerClass() : m_execution_count(0) {}

    void onWorkerExecute(OS::WorkerHandle_t handle) override
    {
        m_execution_count++;
        LOG_DEBUG("Class worker executed, count=%lu\n", m_execution_count);
    }

    uint32_t getExecutionCount() const { return m_execution_count; }

private:
    uint32_t m_execution_count;
};

static bool test_class_worker()
{
    LOG_INFO("Test 4: Class Method Worker\n");

    TestWorkerClass test_obj;

    /* Create worker with class method */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_class",
        300,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        &test_obj
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    /* Start worker */
    bool result = OS::WorkerPool::GetInstance().startWorker(worker);
    if (!result) {
        LOG_ERROR("FAILED: Worker start failed\n");
        return false;
    }

    /* Wait for ~3 executions */
    OS::Delay(1000);

    /* Stop worker */
    OS::WorkerPool::GetInstance().stopWorker(worker);

    uint32_t count = test_obj.getExecutionCount();
    LOG_DEBUG("Execution count: %lu (expected ~3)\n", count);

    if (count < 2 || count > 4) {
        LOG_ERROR("FAILED: Execution count=%lu, expected 2-4\n", count);
        return false;
    }

    LOG_INFO("Test 4: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 5: Change Period                                                     */
/* ========================================================================== */

static void changePeriodCallback(OS::WorkerHandle_t handle)
{
    g_test_counter++;
}

static bool test_change_period()
{
    LOG_INFO("Test 5: Change Period\n");

    resetTestCounters();

    /* Create periodic worker - 1 second period */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_period",
        1000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        changePeriodCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    /* Start worker */
    OS::WorkerPool::GetInstance().startWorker(worker);

    /* Wait 1.5s - should execute once */
    OS::Delay(1500);
    LOG_DEBUG("Count after 1.5s: %lu (expected ~1)\n", g_test_counter);

    if (g_test_counter < 1 || g_test_counter > 2) {
        LOG_ERROR("FAILED: Count=%lu, expected 1-2\n", g_test_counter);
        return false;
    }

    /* Change period to 200ms */
    bool result = OS::WorkerPool::GetInstance().changePeriod(worker, 200);
    if (!result) {
        LOG_ERROR("FAILED: Change period failed\n");
        return false;
    }

    uint32_t count_before = g_test_counter;

    /* Wait 1s - should execute ~5 times with 200ms period */
    OS::Delay(1000);
    uint32_t new_executions = g_test_counter - count_before;

    LOG_DEBUG("New executions: %lu (expected ~5)\n", new_executions);

    if (new_executions < 4 || new_executions > 6) {
        LOG_ERROR("FAILED: New executions=%lu, expected 4-6\n", new_executions);
        return false;
    }

    OS::WorkerPool::GetInstance().stopWorker(worker);

    LOG_INFO("Test 5: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 6: Reset Timer                                                       */
/* ========================================================================== */

static void resetTimerCallback(OS::WorkerHandle_t handle)
{
    g_test_counter++;
    LOG_DEBUG("Reset timer callback, count=%lu\n", g_test_counter);
}

static bool test_reset_timer()
{
    LOG_INFO("Test 6: Reset Timer\n");

    resetTestCounters();

    /* Create periodic worker - 1 second period */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_reset",
        1000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        resetTimerCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    OS::WorkerPool::GetInstance().startWorker(worker);

    /* Reset timer every 500ms, 3 times */
    for (int i = 0; i < 3; i++) {
        OS::Delay(500);
        OS::WorkerPool::GetInstance().resetWorker(worker);
        LOG_DEBUG("Timer reset at %d * 500ms\n", i + 1);
    }

    /* Counter should still be 0 because we keep resetting */
    LOG_DEBUG("Count after resets: %lu (expected 0)\n", g_test_counter);

    if (g_test_counter != 0) {
        LOG_ERROR("FAILED: Count=%lu after resets, expected 0\n", g_test_counter);
        return false;
    }

    /* Now wait for execution */
    OS::Delay(1100);

    /* Should have executed once */
    LOG_DEBUG("Count after wait: %lu (expected 1)\n", g_test_counter);

    if (g_test_counter != 1) {
        LOG_ERROR("FAILED: Count=%lu after wait, expected 1\n", g_test_counter);
        return false;
    }

    OS::WorkerPool::GetInstance().stopWorker(worker);

    LOG_INFO("Test 6: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 7: Force Execute                                                     */
/* ========================================================================== */

static void forceExecuteCallback(OS::WorkerHandle_t handle)
{
    g_test_counter++;
    LOG_DEBUG("Force execute callback, count=%lu\n", g_test_counter);
}

static bool test_force_execute()
{
    LOG_INFO("Test 7: Force Execute\n");

    resetTestCounters();

    /* Create periodic worker - 5 second period (long) */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_force",
        5000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        forceExecuteCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    OS::WorkerPool::GetInstance().startWorker(worker);

    /* Wait a bit */
    OS::Delay(500);

    /* Counter should be 0 (not enough time for normal execution) */
    if (g_test_counter != 0) {
        LOG_ERROR("FAILED: Count=%lu before force, expected 0\n", g_test_counter);
        return false;
    }

    /* Force execute with RESET_TIMER */
    bool result = OS::WorkerPool::GetInstance().forceExecute(
        worker,
        OS::ForceExecuteMode::RESET_TIMER
    );

    if (!result) {
        LOG_ERROR("FAILED: Force execute failed\n");
        return false;
    }

    /* Small delay for execution */
    OS::Delay(100);

    /* Counter should be 1 now */
    LOG_DEBUG("Count after force: %lu (expected 1)\n", g_test_counter);

    if (g_test_counter != 1) {
        LOG_ERROR("FAILED: Count=%lu after force, expected 1\n", g_test_counter);
        return false;
    }

    /* Timer was reset, so wait another 5s for next execution */
    OS::Delay(5100);

    LOG_DEBUG("Count after 5s: %lu (expected 2)\n", g_test_counter);

    if (g_test_counter != 2) {
        LOG_ERROR("FAILED: Count=%lu after 5s, expected 2\n", g_test_counter);
        return false;
    }

    OS::WorkerPool::GetInstance().stopWorker(worker);

    LOG_INFO("Test 7: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 8: Context Data                                                      */
/* ========================================================================== */

struct TestContext {
    uint32_t value;
    const char* message;
};

static void contextCallback(OS::WorkerHandle_t handle)
{
    TestContext* ctx = static_cast<TestContext*>(
        OS::WorkerPool::GetInstance().getContext(handle)
    );

    if (ctx) {
        ctx->value++;
        LOG_DEBUG("Context callback: value=%lu, message=%s\n",
                 ctx->value, ctx->message);
    }
}

static bool test_context_data()
{
    LOG_INFO("Test 8: Context Data\n");

    TestContext ctx = {0, "Hello from context"};

    /* Create worker */
    OS::WorkerHandle_t worker = OS::WorkerPool::GetInstance().createWorker(
        "test_context",
        300,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        contextCallback
    );

    if (worker == nullptr) {
        LOG_ERROR("FAILED: Worker creation failed\n");
        return false;
    }

    /* Set context */
    OS::WorkerPool::GetInstance().setContext(worker, &ctx);

    /* Verify context */
    void* ctx_ptr = OS::WorkerPool::GetInstance().getContext(worker);
    if (ctx_ptr != &ctx) {
        LOG_ERROR("FAILED: Context pointer mismatch\n");
        return false;
    }

    /* Start worker */
    OS::WorkerPool::GetInstance().startWorker(worker);

    /* Wait for executions */
    OS::Delay(1000);

    OS::WorkerPool::GetInstance().stopWorker(worker);

    LOG_DEBUG("Final context value: %lu (expected ~3)\n", ctx.value);

    if (ctx.value < 2 || ctx.value > 4) {
        LOG_ERROR("FAILED: Context value=%lu, expected 2-4\n", ctx.value);
        return false;
    }

    LOG_INFO("Test 8: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 9: Stack Size Selection                                              */
/* ========================================================================== */

static void stackTestCallback(OS::WorkerHandle_t handle)
{
    /* Do nothing */
}

static bool test_stack_selection()
{
    LOG_INFO("Test 9: Stack Size Selection\n");

    /* Test auto-select (0) */
    OS::WorkerHandle_t worker1 = OS::WorkerPool::GetInstance().createWorker(
        "test_stack_auto",
        1000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        0,
        stackTestCallback
    );

    if (worker1 == nullptr) {
        LOG_ERROR("FAILED: Auto-select worker creation failed\n");
        return false;
    }

    uint16_t stack1 = OS::WorkerPool::GetInstance().getActualStackSize(worker1);
    LOG_DEBUG("Auto-select got %u bytes stack\n", stack1);

    if (stack1 != 1024) {
        LOG_ERROR("FAILED: Auto-select got %u bytes, expected 1024\n", stack1);
        return false;
    }

    /* Test specific requirement */
    OS::WorkerHandle_t worker2 = OS::WorkerPool::GetInstance().createWorker(
        "test_stack_2k",
        1000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        1500,
        stackTestCallback
    );

    if (worker2 == nullptr) {
        LOG_ERROR("FAILED: 1500 bytes worker creation failed\n");
        return false;
    }

    uint16_t stack2 = OS::WorkerPool::GetInstance().getActualStackSize(worker2);
    LOG_DEBUG("1500 bytes request got %u bytes stack\n", stack2);

    if (stack2 != 2048) {
        LOG_ERROR("FAILED: 1500 bytes request got %u bytes, expected 2048\n", stack2);
        return false;
    }

    /* Test too large request */
    uint16_t max_stack = OS::WorkerPool::GetInstance().getMaxAvailableStack();
    LOG_DEBUG("Max available stack: %u bytes\n", max_stack);

    OS::WorkerHandle_t worker3 = OS::WorkerPool::GetInstance().createWorker(
        "test_stack_toolarge",
        1000,
        OS::WorkerMode::PERIODIC,
        OS::WorkerPriority::NORMAL,
        max_stack + 1,
        stackTestCallback
    );

    if (worker3 != nullptr) {
        LOG_ERROR("FAILED: Too large stack request should fail\n");
        return false;
    }

    LOG_INFO("Test 9: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Test 10: Multiple Workers                                                 */
/* ========================================================================== */

static volatile uint32_t g_worker_counters[5] = {0};

static void multiWorker1(OS::WorkerHandle_t h) { g_worker_counters[0]++; }
static void multiWorker2(OS::WorkerHandle_t h) { g_worker_counters[1]++; }
static void multiWorker3(OS::WorkerHandle_t h) { g_worker_counters[2]++; }
static void multiWorker4(OS::WorkerHandle_t h) { g_worker_counters[3]++; }
static void multiWorker5(OS::WorkerHandle_t h) { g_worker_counters[4]++; }

static bool test_multiple_workers()
{
    LOG_INFO("Test 10: Multiple Workers\n");

    /* Reset counters */
    for (int i = 0; i < 5; i++) {
        g_worker_counters[i] = 0;
    }

    /* Create 5 workers with different periods */
    OS::WorkerHandle_t workers[5];

    workers[0] = OS::WorkerPool::GetInstance().createWorker(
        "multi1", 200, OS::WorkerMode::PERIODIC, OS::WorkerPriority::NORMAL, 0, multiWorker1);
    workers[1] = OS::WorkerPool::GetInstance().createWorker(
        "multi2", 300, OS::WorkerMode::PERIODIC, OS::WorkerPriority::NORMAL, 0, multiWorker2);
    workers[2] = OS::WorkerPool::GetInstance().createWorker(
        "multi3", 400, OS::WorkerMode::PERIODIC, OS::WorkerPriority::NORMAL, 0, multiWorker3);
    workers[3] = OS::WorkerPool::GetInstance().createWorker(
        "multi4", 500, OS::WorkerMode::PERIODIC, OS::WorkerPriority::HIGH, 0, multiWorker4);
    workers[4] = OS::WorkerPool::GetInstance().createWorker(
        "multi5", 600, OS::WorkerMode::PERIODIC, OS::WorkerPriority::HIGH, 0, multiWorker5);

    /* Verify all created */
    for (int i = 0; i < 5; i++) {
        if (workers[i] == nullptr) {
            LOG_ERROR("FAILED: Worker %d creation failed\n", i + 1);
            return false;
        }
    }

    /* Start all workers */
    for (int i = 0; i < 5; i++) {
        OS::WorkerPool::GetInstance().startWorker(workers[i]);
    }

    /* Run for 2 seconds */
    OS::Delay(2000);

    /* Stop all workers */
    for (int i = 0; i < 5; i++) {
        OS::WorkerPool::GetInstance().stopWorker(workers[i]);
    }

    /* Check execution counts */
    LOG_DEBUG("Worker execution counts after 2s:\n");
    for (int i = 0; i < 5; i++) {
        LOG_DEBUG("  Worker %d: %lu\n", i + 1, g_worker_counters[i]);
    }

    /* Approximate expected counts (2000ms / period) */
    if (g_worker_counters[0] < 8 || g_worker_counters[0] > 12) {
        LOG_ERROR("FAILED: Worker 1 count=%lu, expected 8-12\n", g_worker_counters[0]);
        return false;
    }

    if (g_worker_counters[1] < 5 || g_worker_counters[1] > 8) {
        LOG_ERROR("FAILED: Worker 2 count=%lu, expected 5-8\n", g_worker_counters[1]);
        return false;
    }

    if (g_worker_counters[2] < 4 || g_worker_counters[2] > 6) {
        LOG_ERROR("FAILED: Worker 3 count=%lu, expected 4-6\n", g_worker_counters[2]);
        return false;
    }

    if (g_worker_counters[3] < 3 || g_worker_counters[3] > 5) {
        LOG_ERROR("FAILED: Worker 4 count=%lu, expected 3-5\n", g_worker_counters[3]);
        return false;
    }

    if (g_worker_counters[4] < 2 || g_worker_counters[4] > 4) {
        LOG_ERROR("FAILED: Worker 5 count=%lu, expected 2-4\n", g_worker_counters[4]);
        return false;
    }

    LOG_INFO("Test 10: PASSED\n\n");
    return true;
}

/* ========================================================================== */
/* Main Test Function                                                         */
/* ========================================================================== */

void TestWorkerPool_execute()
{
    LOG_INFO("=============================================================\n");
    LOG_INFO("Worker Pool Test Suite\n");
    LOG_INFO("=============================================================\n\n");

    bool all_passed = true;

    /* Test 1: Initialization */
    if (!test_initialization()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 2: Simple Periodic Worker */
    if (!test_periodic_worker()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 3: One-Shot Worker */
    if (!test_oneshot_worker()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 4: Class Method Worker */
    if (!test_class_worker()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 5: Change Period */
    if (!test_change_period()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 6: Reset Timer */
    if (!test_reset_timer()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 7: Force Execute */
    if (!test_force_execute()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 8: Context Data */
    if (!test_context_data()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 9: Stack Size Selection */
    if (!test_stack_selection()) {
        all_passed = false;
        goto test_end;
    }

    /* Test 10: Multiple Workers */
    if (!test_multiple_workers()) {
        all_passed = false;
        goto test_end;
    }

test_end:
    LOG_INFO("=============================================================\n");
    if (all_passed) {
        LOG_INFO("ALL TESTS PASSED!\n");
    } else {
        LOG_ERROR("TESTS FAILED! See errors above.\n");
    }
    LOG_INFO("=============================================================\n");

    /* Keep running for observation */
    while (1) {
        OS::Delay(10000);
    }
}
