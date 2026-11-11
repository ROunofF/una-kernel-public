#define LOG_MODULE_PRX      "AppQueue"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"
#include "OS.hpp"
#include "Test/TestAppQueue.hpp"
#include "IAppComm.hpp"
#include "ConcreteMessages.hpp"
#include "KernelComm.hpp"
//#include "AppComm.hpp"

class TestApp
{
public:
    TestApp(IAppComm* comm) : mComm(comm) {}

    void run_send_to_kernel()
    {
        LOG_INFO("Sending GPS request to kernel...\n");

        auto* gpsReq = mComm->allocateMessage<GpsRequestMsg>();
        if (!gpsReq) {
            LOG_ERROR("Failed to allocate message\n");
            return;
        }

        // Fill request fields

        LOG_INFO("Message allocated, sending...\n");

        // Send message to kernel
        if (mComm->sendMessage(gpsReq)) {
            if (mComm->waitResponse(gpsReq, 1000)) {
                float lat = gpsReq->latitude;
                float lon = gpsReq->longitude;
                LOG_INFO("Response received: lat=%.6f, lon=%.6f\n", lat, lon);
            }
        }

        LOG_DEBUG("About to release message, refCount=%u\n", gpsReq->getRefCount());
        mComm->releaseMessage(gpsReq);
        LOG_INFO("Message released\n");
    }

private:
    IAppComm* mComm;
};

// Kernel task for processing messages from applications
static void kernelTask(void* params)
{
    LOG_INFO("Kernel task started\n");

    while (1) {
        // Receive message from applications
        MessageBase* msg = KernelComm::getInstance().receiveFromApps(portMAX_DELAY);

        if (msg) {
            LOG_INFO("Kernel received message type: 0x%04X from process: %u\n",
                     static_cast<uint16_t>(msg->getType()), msg->getProcessId());

            // Check if process still exists
            if (!KernelComm::getInstance().isProcessValid(msg->getProcessId())) {
                LOG_WARNING("Process %u no longer exists\n", msg->getProcessId());
                msg->setResult(MessageResult::PROCESS_DESTROYED);
                KernelComm::getInstance().releaseMessage(msg);
                continue;
            }

            // Process message based on type
            switch (msg->getType()) {
                case MessageType::REQUEST_GPS_LOCATION: {
                    auto* gpsReq = static_cast<GpsRequestMsg*>(msg);

                    LOG_INFO("Processing GPS request\n");

                    // Simulate GPS processing
                    gpsReq->latitude = 45.0000f;   // Test point in the open Atlantic
                    gpsReq->longitude = -30.0000f; // Test point in the open Atlantic
                    gpsReq->altitude = 179.0f;
                    gpsReq->accuracy = 5.0f;
                    gpsReq->fixAvailable = true;
                    gpsReq->setResult(MessageResult::SUCCESS);

                    LOG_INFO("GPS data prepared, signaling completion\n");

                    // Signal completion if response expected
                    if (gpsReq->needsResponse()) {
                        LOG_INFO("Signaling completion\n");
                        KernelComm::getInstance().signalCompletion(gpsReq);
                    }

                    break;
                }

                default:
                    LOG_WARNING("Unknown message type: 0x%04X\n",
                             static_cast<uint16_t>(msg->getType()));
                    msg->setResult(MessageResult::FAIL);
                    break;
            }

            // Release message
            LOG_DEBUG("Kernel about to release message, refCount=%u\n", msg->getRefCount());
            KernelComm::getInstance().releaseMessage(msg);
            LOG_INFO("Message processed and released\n");
        }
    }
}

// Global variables
static AppComm* g_testAppComm = nullptr;
static TaskHandle_t g_kernelTaskHandle = nullptr;

void TestAppQueue_execute()
{
    LOG_INFO("Initializing KernelComm system...\n");

    // Initialize kernel communication system
    KernelComm::getInstance().init();

    // Create kernel task for processing messages
    xTaskCreate(
        kernelTask,
        "KernelTask",
        512,
        nullptr,
        tskIDLE_PRIORITY + 2,
        &g_kernelTaskHandle
    );

    LOG_INFO("Creating AppComm for test application...\n");

    // Create AppComm with process ID = 1
    g_testAppComm = new AppComm(1, 10);  // processId=1, queueSize=10

    // Register application with kernel
    if (!KernelComm::getInstance().registerProcess(g_testAppComm)) {
        LOG_ERROR("Failed to register process\n");
        return;
    }

    LOG_INFO("Test application registered with processId: 1\n");

    // Create test application instance
    TestApp* test = new TestApp(g_testAppComm);

    LOG_INFO("Starting test loop...\n");

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING APP QUEUE\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test->run_send_to_kernel();

        OS::Delay(3000);
    }
}
