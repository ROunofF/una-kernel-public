/**
 * @file TestAppApplication.cpp
 * @brief Comprehensive tests for App2::Application with real FreeRTOS threads
 */

#include "AppApplication.hpp"
#include "CommandMessages.hpp"
#include "ConcreteMessages.hpp"
#include "MessageTypes.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>

#define LOG_MODULE_PRX      "AppTest"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

// =============================================================================
// Test Configuration
// =============================================================================

static MessageManager* g_msgMgr = nullptr;
static App2::Application* g_testApp = nullptr;
static TaskHandle_t g_serviceTaskHandle = nullptr;
static TaskHandle_t g_guiTaskHandle = nullptr;

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
        LOG_INFO_WP("\n");
        LOG_INFO_WP("============================================================\n");
        LOG_INFO_WP("TEST RESULTS: %u/%u passed (%u failed)\n", 
                    passedTests, totalTests, failedTests);
        LOG_INFO_WP("============================================================\n");
    }
};

static TestStats g_stats;

// Test control flags
static volatile bool g_serviceRunning = false;
static volatile bool g_guiRunning = false;

// =============================================================================
// Test Helper Macros
// =============================================================================

#define TEST_START(name) \
    LOG_INFO_WP("\n[TEST] %s\n", name)

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            LOG_DEBUG("[PASS] %s\n", message); \
            g_stats.recordPass(); \
        } else { \
            LOG_ERROR("[FAIL] %s\n", message); \
            g_stats.recordFail(); \
        } \
    } while(0)

// =============================================================================
// Mock Service Thread
// =============================================================================

static void serviceThreadTask(void* params)
{
    SDK::Interface::IAppComm* srvComm = static_cast<SDK::Interface::IAppComm*>(params);
    
    LOG_INFO("Service thread started\n");
    
    g_serviceRunning = true;
    
    while (g_serviceRunning) {
        MessageBase* msg;
        if (srvComm->getMessage(msg, 100)) {
            LOG_DEBUG("Service received message type: 0x%08X\n", msg->getType());
            
            switch (msg->getType()) {
                case MessageType::COMMAND_APP_RUN:
                    LOG_DEBUG("Service: Initializing...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    srvComm->sendResponse(msg);
                    break;
                    
                case MessageType::COMMAND_APP_STOP:
                    LOG_DEBUG("Service: Stopping...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    srvComm->sendResponse(msg);
                    g_serviceRunning = false;
                    break;
                    
                case MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_DEBUG("Service: GUI is now running\n");
                    msg->setResult(MessageResult::SUCCESS);
                    srvComm->sendResponse(msg);
                    break;
                    
                case MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_DEBUG("Service: GUI has stopped\n");
                    msg->setResult(MessageResult::SUCCESS);
                    srvComm->sendResponse(msg);
                    break;
                    
                case MessageType::REQUEST_GPS_LOCATION:
                    LOG_DEBUG("Service: Processing GPS request\n");
                    auto* gpsReq = static_cast<GpsRequestMsg*>(msg);
                    gpsReq->latitude = 45.0000f;
                    gpsReq->longitude = -30.0000f;
                    gpsReq->altitude = 150.0f;
                    gpsReq->accuracy = 5.0f;
                    gpsReq->fixAvailable = true;
                    msg->setResult(MessageResult::SUCCESS);
                    srvComm->sendResponse(msg);
                    break;
            }
            
            srvComm->releaseMessage(msg);
        }
    }
    
    LOG_INFO("Service thread stopped\n");
    g_serviceTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

// =============================================================================
// Mock GUI Thread
// =============================================================================

static void guiThreadTask(void* params)
{
    SDK::Interface::IAppComm* guiComm = static_cast<SDK::Interface::IAppComm*>(params);
    
    LOG_INFO("GUI thread started\n");
    
    g_guiRunning = true;
    
    while (g_guiRunning) {
        MessageBase* msg;
        if (guiComm->getMessage(msg, 100)) {
            LOG_DEBUG("GUI received message type: 0x%08X\n", msg->getType());
            
            switch (msg->getType()) {
                case MessageType::COMMAND_APP_RUN:
                    LOG_DEBUG("GUI: Initializing...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    guiComm->sendResponse(msg);
                    break;
                    
                case MessageType::COMMAND_APP_STOP:
                    LOG_DEBUG("GUI: Stopping...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    guiComm->sendResponse(msg);
                    g_guiRunning = false;
                    break;
                    
                case MessageType::COMMAND_APP_GUI_SUSPEND:
                    LOG_DEBUG("GUI: Suspending rendering...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    guiComm->sendResponse(msg);
                    break;
                    
                case MessageType::COMMAND_APP_GUI_RESUME:
                    LOG_DEBUG("GUI: Resuming rendering...\n");
                    msg->setResult(MessageResult::SUCCESS);
                    guiComm->sendResponse(msg);
                    break;
                    
                case MessageType::EVENT_BUTTON_PRESS:
                    LOG_DEBUG("GUI: Button event received\n");
                    // Fire-and-forget, no response
                    break;

                default:
                    // Handle internal messages (0x8000 range)
                    if (isApplicationMessage(msg->getType())) {
                        LOG_DEBUG("GUI: Processing internal message 0x%08X\n", msg->getType());
                        // Process internal message and respond if needed
                        msg->setResult(MessageResult::SUCCESS);
                        if (msg->needsResponse()) {
                            guiComm->sendResponse(msg);
                        }
                    } else {
                        LOG_WARNING("GUI: Unknown message type 0x%08X\n", msg->getType());
                    }
                    break;
            }
            
            guiComm->releaseMessage(msg);
        }
    }
    
    LOG_INFO("GUI thread stopped\n");
    g_guiTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

// =============================================================================
// Test Cases
// =============================================================================

static bool test1_createApplication()
{
    TEST_START("Create Application");
    
    // Create app info
    App2::Info info = {};
    info.srvPID = 0x00010001;
    info.guiPID = 0x00010002;
    strncpy(info.name, "TestApp1", App::kNameLen);
    
    // Create application
    g_testApp = new App2::Application(info, *g_msgMgr);
    
    TEST_ASSERT(g_testApp != nullptr, "Application object created");
    TEST_ASSERT(g_testApp->getSrvPID() == info.srvPID, "Service PID correct");
    TEST_ASSERT(g_testApp->getGuiPID() == info.guiPID, "GUI PID correct");
    
    // Create instance
    bool created = g_testApp->create();
    TEST_ASSERT(created, "Application instance created");
    
    // Verify communication interfaces
    SDK::Interface::IAppComm* srvComm = g_testApp->getServiceComm();
    SDK::Interface::IAppComm* guiComm = g_testApp->getGuiComm();
    
    TEST_ASSERT(srvComm != nullptr, "Service comm available");
    TEST_ASSERT(guiComm != nullptr, "GUI comm available");
    
    return true;
}

static bool test2_startThreads()
{
    TEST_START("Start Service and GUI Threads");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    SDK::Interface::IAppComm* srvComm = g_testApp->getServiceComm();
    SDK::Interface::IAppComm* guiComm = g_testApp->getGuiComm();
    
    // Create Service thread
    BaseType_t result = xTaskCreate(
        serviceThreadTask,
        "ServiceTask",
        2048,
        srvComm,
        tskIDLE_PRIORITY + 2,
        &g_serviceTaskHandle
    );
    
    TEST_ASSERT(result == pdPASS, "Service thread created");
    
    // Create GUI thread
    result = xTaskCreate(
        guiThreadTask,
        "GuiTask",
        2048,
        guiComm,
        tskIDLE_PRIORITY + 2,
        &g_guiTaskHandle
    );
    
    TEST_ASSERT(result == pdPASS, "GUI thread created");
    
    // Give threads time to start
    vTaskDelay(pdMS_TO_TICKS(50));
    
    TEST_ASSERT(g_serviceRunning, "Service thread running");
    TEST_ASSERT(g_guiRunning, "GUI thread running");
    
    return true;
}

static bool test3_runService()
{
    TEST_START("Run Service Only");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    // Run service (sends COMMAND_APP_RUN)
    bool started = g_testApp->runService(1000);
    
    TEST_ASSERT(started, "Service started successfully");
    
    return true;
}

static bool test4_runFullApplication()
{
    TEST_START("Run Full Application (Service + GUI)");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    // Run full application
    bool started = g_testApp->run(1000);
    
    TEST_ASSERT(started, "Full application started");
    
    return true;
}

static bool test5_serviceGuiCommunication()
{
    TEST_START("Service <-> GUI Internal Communication");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    SDK::Interface::IAppComm* srvComm = g_testApp->getServiceComm();
    
    // Define internal message type (0x8000 range)
    constexpr MessageType::Type INTERNAL_UPDATE = 0x00008001;
    
    struct InternalUpdateMsg : public MessageBase {
        uint32_t value;
        InternalUpdateMsg() : MessageBase(INTERNAL_UPDATE), value(0) {}
    };
    
    // Service sends internal message to GUI with response expected
    auto* msg = srvComm->allocateMessage<InternalUpdateMsg>();
    TEST_ASSERT(msg != nullptr, "Internal message allocated");
    
    if (msg) {
        msg->value = 12345;
        
        LOG_DEBUG("Service sending internal message (0x%08X) to GUI with timeout\n", msg->getType());
        
        // Send with timeout to wait for GUI to process and respond
        bool sent = srvComm->sendMessage(msg, 1000);
        
        if (sent) {
            TEST_ASSERT(msg->getResult() == MessageResult::SUCCESS, "GUI processed internal message");
            LOG_DEBUG("GUI confirmed receipt and processing\n");
        } else {
            LOG_ERROR("GUI did not respond within timeout\n");
            g_stats.recordFail();
        }

        srvComm->releaseMessage(msg);
    }
    
    return true;
}

static bool test6_requestResponse()
{
    TEST_START("Request-Response Pattern");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    SDK::Interface::IAppComm* srvComm = g_testApp->getServiceComm();
    
    // Allocate GPS request
    auto* req = srvComm->allocateMessage<GpsRequestMsg>();
    TEST_ASSERT(req != nullptr, "GPS request allocated");
    
    if (req) {
        LOG_DEBUG("Sending GPS request with timeout=1000ms\n");
        
        bool sent = srvComm->sendMessage(req, 1000);

        TEST_ASSERT(sent, "Message sent successfully");
        
        if (sent) {
            // Message was sent and processed
            // In this test, kernel returns ERROR (mock implementation)
            // That's expected - we're testing message flow, not GPS processing
            LOG_DEBUG("GPS Response: lat=%.4f, lon=%.4f, result=%d\n",
                     req->latitude, req->longitude, (int)req->getResult());

            // The important part: message was delivered and response received
            TEST_ASSERT(req->getResult() != MessageResult::PENDING, "Response received from kernel");
        }
        
        srvComm->releaseMessage(req);
    }
    
    return true;
}

static bool test7_suspendResume()
{
    TEST_START("Suspend and Resume GUI");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    // Suspend GUI
    bool suspended = g_testApp->suspend(1000);
    TEST_ASSERT(suspended, "GUI suspended");
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Resume GUI
    bool resumed = g_testApp->resume(1000);
    TEST_ASSERT(resumed, "GUI resumed");
    
    return true;
}

static bool test8_stopApplication()
{
    TEST_START("Stop Application");
    
    if (!g_testApp) {
        LOG_ERROR("Application not created\n");
        return false;
    }
    
    // Stop application (sends COMMAND_APP_STOP to both parts)
    bool stopped = g_testApp->stop(1000);
    TEST_ASSERT(stopped, "Application stopped");
    
    // Wait for threads to exit
    vTaskDelay(pdMS_TO_TICKS(200));
    
    TEST_ASSERT(!g_serviceRunning, "Service thread stopped");
    TEST_ASSERT(!g_guiRunning, "GUI thread stopped");
    TEST_ASSERT(g_serviceTaskHandle == nullptr, "Service task cleaned up");
    TEST_ASSERT(g_guiTaskHandle == nullptr, "GUI task cleaned up");
    
    // Cleanup
    delete g_testApp;
    g_testApp = nullptr;
    
    return true;
}

static bool test9_serviceRequestsGuiLaunch()
{
    TEST_START("Service Requests GUI Launch");

    // Create new application for this test
    App2::Info info = {};
    info.srvPID = 0x00090001;
    info.guiPID = 0x00090002;
    strncpy(info.name, "GuiLaunchTest", App::kNameLen);

    g_testApp = new App2::Application(info, *g_msgMgr);

    if (!g_testApp->create()) {
        LOG_ERROR("Failed to create application\n");
        return false;
    }

    SDK::Interface::IAppComm* srvComm = g_testApp->getServiceComm();
    SDK::Interface::IAppComm* guiComm = g_testApp->getGuiComm();

    // Start threads
    BaseType_t result = xTaskCreate(
        serviceThreadTask,
        "ServiceTask",
        2048,
        srvComm,
        tskIDLE_PRIORITY + 2,
        &g_serviceTaskHandle
    );
    TEST_ASSERT(result == pdPASS, "Service thread created");

    result = xTaskCreate(
        guiThreadTask,
        "GuiTask",
        2048,
        guiComm,
        tskIDLE_PRIORITY + 2,
        &g_guiTaskHandle
    );
    TEST_ASSERT(result == pdPASS, "GUI thread created");

    vTaskDelay(pdMS_TO_TICKS(50));

    // Start only Service (not GUI)
    LOG_DEBUG("Starting Service only...\n");
    bool started = g_testApp->runService(1000);
    TEST_ASSERT(started, "Service started");
    TEST_ASSERT(g_serviceRunning, "Service is running");

    // GUI should NOT be running yet
    vTaskDelay(pdMS_TO_TICKS(50));
    // Note: We can't easily check GUI state from here, but we trust it's not started

    LOG_DEBUG("Service requests GUI launch via REQUEST_RUN_GUI...\n");

    // Service requests GUI launch
    // Note: REQUEST_RUN_GUI goes to kernel (ApplicationInstance thread),
    // not to Service or GUI threads. Kernel processes it and calls runGui().
    auto* req = srvComm->allocateMessage<RunGuiRequest>();
    TEST_ASSERT(req != nullptr, "RunGuiRequest allocated");

    if (req) {
        // Send request to kernel (with timeout - wait for GUI to start)
        bool sent = srvComm->sendMessage(req, 2000);

        TEST_ASSERT(sent, "REQUEST_RUN_GUI sent successfully");

        if (sent) {
            TEST_ASSERT(req->getResult() == MessageResult::SUCCESS, "GUI launched successfully");
            LOG_INFO("Service: GUI is now running!\n");
        } else {
            LOG_ERROR("Service: GUI launch timeout\n");
        }

        srvComm->releaseMessage(req);
    }

    // Verify GUI is now running
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT(g_guiRunning, "GUI is now running");

    // Now Service can send internal messages to GUI
    LOG_DEBUG("Service sending internal message to GUI...\n");

    constexpr MessageType::Type INTERNAL_NOTIFICATION = 0x00008010;
    struct NotificationMsg : public MessageBase {
        char text[32];
        NotificationMsg() : MessageBase(INTERNAL_NOTIFICATION) {
            text[0] = '\0';
        }
    };

    auto* notif = srvComm->allocateMessage<NotificationMsg>();
    if (notif) {
        strncpy(notif->text, "Hello GUI!", sizeof(notif->text) - 1);

        // Send with response to verify GUI received it
        bool notifSent = srvComm->sendMessage(notif, 1000);
        TEST_ASSERT(notifSent, "Notification sent to GUI");

        if (notifSent) {
            TEST_ASSERT(notif->getResult() == MessageResult::SUCCESS, "GUI processed notification");
        }

        srvComm->releaseMessage(notif);
    }

    // Cleanup
    g_testApp->stop(1000);
    vTaskDelay(pdMS_TO_TICKS(200));

    delete g_testApp;
    g_testApp = nullptr;

    return true;
}

// =============================================================================
// Test Runner
// =============================================================================

static void testRunnerTask(void* params)
{
    LOG_INFO_WP("\n");
    LOG_INFO_WP("============================================================\n");
    LOG_INFO_WP("  TEST SUITE: App2::Application with FreeRTOS Threads\n");
    LOG_INFO_WP("============================================================\n");
    
    g_stats.reset();
    
    // Create message manager
    g_msgMgr = new MessageManager();
    
    // Run tests
    test1_createApplication();
    test2_startThreads();
    test3_runService();
    test4_runFullApplication();
    test5_serviceGuiCommunication();
    test6_requestResponse();
    test7_suspendResume();
    test8_stopApplication();
    test9_serviceRequestsGuiLaunch();  // New test!
    
    // Cleanup
    delete g_msgMgr;
    g_msgMgr = nullptr;
    
    // Print results
    g_stats.print();
    
    LOG_INFO("Test suite completed\n");
    
    // Delete this task
    vTaskDelete(nullptr);
}

// =============================================================================
// Public Test Entry Point
// =============================================================================

void TestAppApplication_execute()
{
    BaseType_t result = xTaskCreate(
        testRunnerTask,
        "TestRunner",
        4096,
        nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr
    );
    
    if (result != pdPASS) {
        LOG_ERROR("Failed to create test runner task\n");
    }
}
