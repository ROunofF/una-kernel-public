/**
 * @file TestAppQueue_Extended.cpp
 * @brief Extended test suite for kernel-app communication system
 * 
 * Tests various scenarios:
 * - Successful request-response
 * - Timeout scenarios
 * - Fire-and-forget events
 * - Invalid process handling
 * - Multiple concurrent requests
 * - Error responses
 */

#include "IAppComm.hpp"
//#include "AppComm.hpp"
#include "KernelComm.hpp"
#include "ConcreteMessages.hpp"
#include "FreeRTOS.h"
#include "task.h"

#define LOG_MODULE_PRX      "AppQueueTest"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestAppQueue_Extended.hpp"

// =============================================================================
// Test Configuration
// =============================================================================

static AppComm* g_testAppComm = nullptr;
static TaskHandle_t g_kernelTaskHandle = nullptr;

// Test statistics
struct TestStats {
    uint32_t totalTests;
    uint32_t passedTests;
    uint32_t failedTests;
    
    void reset() {
        totalTests = 0;
        passedTests = 0;
        failedTests = 0;
    }
    
    void recordPass() {
        totalTests++;
        passedTests++;
    }
    
    void recordFail() {
        totalTests++;
        failedTests++;
    }
    
    void print() {
        LOG_INFO_WP("============================================================\n");
        LOG_INFO_WP("TEST RESULTS: %u/%u passed (%u failed)\n", 
                    passedTests, totalTests, failedTests);
        LOG_INFO_WP("============================================================\n");
    }
};

static TestStats g_stats;

// =============================================================================
// Test Helper Macros
// =============================================================================

#define TEST_START(name) \
    LOG_INFO_WP("\n[TEST] %s\n", name)

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            LOG_INFO("[PASS] %s\n", message); \
            g_stats.recordPass(); \
        } else { \
            LOG_ERROR("[FAIL] %s\n", message); \
            g_stats.recordFail(); \
        } \
    } while(0)

// =============================================================================
// Mock Kernel Task
// =============================================================================

// Kernel behavior flags
static bool g_kernelShouldTimeout = false;
static bool g_kernelShouldReturnError = false;
static uint32_t g_kernelProcessingDelayMs = 0;

static void kernelTask(void* params)
{
    LOG_INFO("Kernel task started\n");
    
    while (1) {
        // Receive message from applications
        MessageBase* msg = KernelComm::getInstance().receiveFromApps(portMAX_DELAY);
        
        if (msg) {
            LOG_DEBUG("Kernel received message type: 0x%04X from process: %u\n", 
                     static_cast<uint16_t>(msg->getType()), msg->getProcessId());
            
            // Check if process still exists
            if (msg->getProcessId() == 0) {
                LOG_ERROR("Message has invalid processId (0)\n");
                msg->setResult(MessageResult::FAIL);
                KernelComm::getInstance().releaseMessage(msg);
                continue;
            }
            
            if (!KernelComm::getInstance().isProcessValid(msg->getProcessId())) {
                LOG_WARNING("Process %u no longer exists\n", msg->getProcessId());
                msg->setResult(MessageResult::PROCESS_DESTROYED);
                KernelComm::getInstance().releaseMessage(msg);
                continue;
            }
            
            // Simulate processing delay if configured
            if (g_kernelProcessingDelayMs > 0) {
                vTaskDelay(pdMS_TO_TICKS(g_kernelProcessingDelayMs));
            }
            
            // Process message based on type
            switch (msg->getType()) {
                case MessageType::REQUEST_GPS_LOCATION: {
                    auto* gpsReq = static_cast<GpsRequestMsg*>(msg);
                    
                    LOG_DEBUG("Processing GPS request\n");
                    
                    if (g_kernelShouldReturnError) {
                        // Simulate error
                        gpsReq->setResult(MessageResult::FAIL);
                    } else if (g_kernelShouldTimeout) {
                        // Simulate timeout by not responding
                        LOG_DEBUG("Simulating timeout - not responding\n");
                        // Don't signal, don't release, just continue
                        continue;
                    } else {
                        // Normal processing
                        gpsReq->latitude = 45.0000f;   // Test point in the open Atlantic
                        gpsReq->longitude = -30.0000f; // Test point in the open Atlantic
                        gpsReq->altitude = 179.0f;
                        gpsReq->accuracy = 5.0f;
                        gpsReq->fixAvailable = true;
                        gpsReq->setResult(MessageResult::SUCCESS);
                    }
                    
                    // Signal completion if response expected
                    if (gpsReq->needsResponse()) {
                        LOG_DEBUG("Signaling completion\n");
                        KernelComm::getInstance().signalCompletion(gpsReq);
                    }
                    
                    break;
                }
                
                case MessageType::REQUEST_BATTERY_STATUS: {
                    auto* battReq = static_cast<BatteryRequestMsg*>(msg);
                    
                    LOG_DEBUG("Processing Battery request\n");
                    
                    if (g_kernelShouldReturnError) {
                        battReq->setResult(MessageResult::FAIL);
                    } else {
                        battReq->batteryLevel = 85;
                        battReq->isCharging = false;
                        battReq->voltage = 3.95f;
                        battReq->setResult(MessageResult::SUCCESS);
                    }
                    
                    if (battReq->needsResponse()) {
                        KernelComm::getInstance().signalCompletion(battReq);
                    }
                    
                    break;
                }
                
                case MessageType::EVENT_BUTTON_PRESS: {
                    LOG_DEBUG("Received button event (fire-and-forget)\n");
                    // Just acknowledge, no response needed
                    break;
                }
                
                default:
                    LOG_WARNING("Unknown message type: 0x%04X\n",
                             static_cast<uint16_t>(msg->getType()));
                    msg->setResult(MessageResult::FAIL);
                    break;
            }
            
            // Release message
            KernelComm::getInstance().releaseMessage(msg);
            LOG_DEBUG("Kernel released message\n");
        }
    }
}

// =============================================================================
// Test Cases
// =============================================================================

class TestSuite {
private:
    IAppComm* mComm;
    
public:
    TestSuite(IAppComm* comm) : mComm(comm) {}
    
    // Test 1: Successful request-response
    void testSuccessfulRequest() {
        TEST_START("Successful GPS Request-Response");
        
        // Reset kernel flags
        g_kernelShouldTimeout = false;
        g_kernelShouldReturnError = false;
        g_kernelProcessingDelayMs = 0;
        
        auto* gpsReq = mComm->allocateMessage<GpsRequestMsg>();
        TEST_ASSERT(gpsReq != nullptr, "Message allocation");
        
        if (gpsReq) {
            bool sent = mComm->sendMessage(gpsReq);
            TEST_ASSERT(sent, "Message send");
            
            if (sent) {
                bool responded = mComm->waitResponse(gpsReq, 1000);
                TEST_ASSERT(responded, "Response received within timeout");
                TEST_ASSERT(gpsReq->getResult() == MessageResult::SUCCESS, "Result is SUCCESS");
                TEST_ASSERT(gpsReq->fixAvailable, "GPS fix available");
                
                LOG_INFO("GPS data: lat=%.6f, lon=%.6f\n", 
                        gpsReq->latitude, gpsReq->longitude);
            }
            
            mComm->releaseMessage(gpsReq);
        }
    }
    
    // Test 2: Request timeout
    void testRequestTimeout() {
        TEST_START("Request Timeout Scenario");
        
        // Configure kernel to not respond
        g_kernelShouldTimeout = true;
        
        auto* gpsReq = mComm->allocateMessage<GpsRequestMsg>();
        TEST_ASSERT(gpsReq != nullptr, "Message allocation");
        
        if (gpsReq) {
            bool sent = mComm->sendMessage(gpsReq);
            TEST_ASSERT(sent, "Message send");
            
            if (sent) {
                bool responded = mComm->waitResponse(gpsReq, 500);
                TEST_ASSERT(!responded, "Timeout occurred as expected");
                TEST_ASSERT(gpsReq->getResult() == MessageResult::TIMEOUT, 
                           "Result is TIMEOUT");
            }
            
            mComm->releaseMessage(gpsReq);
        }
        
        // Reset flag
        g_kernelShouldTimeout = false;
        
        // Give kernel time to process and release the stuck message
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Test 3: Error response
    void testErrorResponse() {
        TEST_START("Error Response Scenario");
        
        // Configure kernel to return error
        g_kernelShouldReturnError = true;
        
        auto* gpsReq = mComm->allocateMessage<GpsRequestMsg>();
        TEST_ASSERT(gpsReq != nullptr, "Message allocation");
        
        if (gpsReq) {
            bool sent = mComm->sendMessage(gpsReq);
            TEST_ASSERT(sent, "Message send");
            
            if (sent) {
                bool responded = mComm->waitResponse(gpsReq, 1000);
                TEST_ASSERT(responded, "Response received (even with error)");
                TEST_ASSERT(gpsReq->getResult() == MessageResult::FAIL, 
                           "Result is ERROR as expected");
            }
            
            mComm->releaseMessage(gpsReq);
        }
        
        // Reset flag
        g_kernelShouldReturnError = false;
    }
    
    // Test 4: Fire-and-forget event
    void testFireAndForgetEvent() {
        TEST_START("Fire-and-Forget Event");
        
        auto* btnEvent = mComm->allocateMessage<ButtonEventMsg>();
        TEST_ASSERT(btnEvent != nullptr, "Event allocation");
        
        if (btnEvent) {
            btnEvent->buttonId = 1;
            btnEvent->pressed = true;
            btnEvent->timestamp = 12345;
            
            bool sent = mComm->sendMessage(btnEvent);
            TEST_ASSERT(sent, "Event send");
            TEST_ASSERT(!btnEvent->needsResponse(), "Event does not need response");
            
            // Can release immediately for fire-and-forget
            mComm->releaseMessage(btnEvent);
            
            // Give kernel time to process
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    // Test 5: Multiple sequential requests
    void testMultipleSequentialRequests() {
        TEST_START("Multiple Sequential Requests");
        
        g_kernelShouldReturnError = false;
        g_kernelShouldTimeout = false;
        
        const int NUM_REQUESTS = 5;
        int successCount = 0;
        
        for (int i = 0; i < NUM_REQUESTS; i++) {
            auto* battReq = mComm->allocateMessage<BatteryRequestMsg>();
            
            if (battReq && mComm->sendMessage(battReq)) {
                if (mComm->waitResponse(battReq, 1000)) {
                    if (battReq->getResult() == MessageResult::SUCCESS) {
                        successCount++;
                        LOG_DEBUG("Request %d: battery=%u%%\n", i, battReq->batteryLevel);
                    }
                }
                mComm->releaseMessage(battReq);
            }
            
            vTaskDelay(pdMS_TO_TICKS(50)); // Small delay between requests
        }
        
        TEST_ASSERT(successCount == NUM_REQUESTS, 
                   "All sequential requests succeeded");
    }
    
    // Test 6: Fast consecutive requests
    void testFastConsecutiveRequests() {
        TEST_START("Fast Consecutive Requests (No Delay)");
        
        const int NUM_REQUESTS = 3;
        GpsRequestMsg* requests[NUM_REQUESTS] = {nullptr};
        
        // Send all requests quickly
        for (int i = 0; i < NUM_REQUESTS; i++) {
            requests[i] = mComm->allocateMessage<GpsRequestMsg>();
            if (requests[i]) {
                bool sent = mComm->sendMessage(requests[i]);
                TEST_ASSERT(sent, "Fast request send");
            }
        }
        
        // Wait for all responses
        int successCount = 0;
        for (int i = 0; i < NUM_REQUESTS; i++) {
            if (requests[i]) {
                if (mComm->waitResponse(requests[i], 1000)) {
                    if (requests[i]->getResult() == MessageResult::SUCCESS) {
                        successCount++;
                    }
                }
                mComm->releaseMessage(requests[i]);
            }
        }
        
        TEST_ASSERT(successCount == NUM_REQUESTS, 
                   "All fast requests succeeded");
    }
    
    // Test 7: Kernel processing delay
    void testKernelProcessingDelay() {
        TEST_START("Kernel Processing Delay");
        
        // Configure kernel to take 300ms to process
        g_kernelProcessingDelayMs = 300;
        
        auto* gpsReq = mComm->allocateMessage<GpsRequestMsg>();
        
        if (gpsReq && mComm->sendMessage(gpsReq)) {
            // Should succeed with 1000ms timeout
            bool responded = mComm->waitResponse(gpsReq, 1000);
            TEST_ASSERT(responded, "Response received despite processing delay");
            TEST_ASSERT(gpsReq->getResult() == MessageResult::SUCCESS, 
                       "Result is SUCCESS");
            
            mComm->releaseMessage(gpsReq);
        }
        
        // Reset delay
        g_kernelProcessingDelayMs = 0;
    }
    
    // Test 8: Allocation failure simulation
    void testAllocationFailure() {
        TEST_START("Handle Allocation Failure");
        
        // Try to allocate many messages to exhaust pool
        const int MAX_ALLOC = 50;
        MessageBase* messages[MAX_ALLOC];
        int allocCount = 0;
        
        // Allocate until failure
        for (int i = 0; i < MAX_ALLOC; i++) {
            messages[i] = mComm->allocateMessage<GpsRequestMsg>();
            if (messages[i]) {
                allocCount++;
            } else {
                LOG_INFO("Pool exhausted after %d allocations\n", allocCount);
                break;
            }
        }
        
        TEST_ASSERT(allocCount > 0, "At least some allocations succeeded");
        
        // Release all
        for (int i = 0; i < allocCount; i++) {
            mComm->releaseMessage(messages[i]);
        }
        
        // Should be able to allocate again
        auto* msg = mComm->allocateMessage<GpsRequestMsg>();
        TEST_ASSERT(msg != nullptr, "Allocation works after release");
        if (msg) {
            mComm->releaseMessage(msg);
        }
    }
    
    void runAllTests() {
        LOG_INFO_WP("\n");
        LOG_INFO_WP("============================================================\n");
        LOG_INFO_WP("STARTING EXTENDED TEST SUITE\n");
        LOG_INFO_WP("============================================================\n");
        
        g_stats.reset();
        
        testSuccessfulRequest();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testRequestTimeout();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testErrorResponse();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testFireAndForgetEvent();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testMultipleSequentialRequests();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testFastConsecutiveRequests();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testKernelProcessingDelay();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        testAllocationFailure();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        g_stats.print();
    }
};

// =============================================================================
// Main Test Entry Point
// =============================================================================

void TestAppQueue_Extended_execute()
{
    LOG_INFO("Initializing extended test suite...\n");
    
    // Initialize kernel communication system
    KernelComm::getInstance().init();
    
    // Create kernel task
    xTaskCreate(
        kernelTask,
        "KernelTask",
        512,
        nullptr,
        tskIDLE_PRIORITY + 2,
        &g_kernelTaskHandle
    );
    
    // Give kernel task time to start
    vTaskDelay(pdMS_TO_TICKS(100));
    
    LOG_INFO("Creating AppComm for test application...\n");
    
    // Create AppComm with process ID = 1
    g_testAppComm = new AppComm(1, 10);
    
    // Register application with kernel
    if (!KernelComm::getInstance().registerProcess(g_testAppComm)) {
        LOG_ERROR("Failed to register process\n");
        return;
    }
    
    LOG_INFO("Test application registered with processId: 1\n");
    
    // Create and run test suite
    TestSuite suite(g_testAppComm);
    
    // Run tests once
    suite.runAllTests();
    
    LOG_INFO_WP("\nTest suite completed. Looping with basic test...\n\n");
    
    // Continue with basic test loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        LOG_INFO_WP("---- Quick Test ----\n");
        suite.testSuccessfulRequest();
    }
}
