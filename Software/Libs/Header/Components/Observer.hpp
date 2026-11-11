/**
 ******************************************************************************
 * @file    Observer.hpp
 * @brief   This is a simple implementation of an observer pattern.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __OBSERVER_HPP
#define __OBSERVER_HPP

#if __cplusplus < 201703L
#error "This code requires at least C++17"
#endif

#include <cstdint>
#include <cstdbool>
#include <functional>
#include <tuple>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

namespace Observer {


class Dispatcher {
public:
    static constexpr std::size_t QUEUE_SIZE = 10;
    static constexpr std::size_t MAX_EVENT_SIZE = 64;

    struct EventBase {
        virtual ~EventBase() = default;
        virtual void call() = 0;
    };

    template<typename T>
    struct Event : public EventBase {
        T *pObserver;
        void (T::*method)();

        Event(T *obs, void (T::*m)()) : pObserver(obs), method(m) {}

        void call() override {
            (pObserver->*method)();
        }
    };

    // Raw Bytes Container
    struct alignas(std::max_align_t) EventStorage {
        char data[MAX_EVENT_SIZE];
    };

    static void init() {
        if (!mQueue) {
            mQueue = xQueueCreateStatic(QUEUE_SIZE, sizeof(EventStorage),
                reinterpret_cast<uint8_t*>(mQueueStorage), &mQueueStruct);
            mTaskHandle = xTaskCreateStatic(taskHandler, "Observer::Dispatcher",
                STACK_SIZE_W, nullptr, TASK_PRIORITY, mTaskStack, &mTaskStruct);
        }
    }

    template<typename T>
    static void dispatch(T *pObserver, void (T::*method)())
    {
        static_assert(sizeof(Event<T>) <= MAX_EVENT_SIZE,
                "Event<T> size exceeds MAX_EVENT_SIZE!");

        EventStorage storage;
        new (storage.data) Event<T>(pObserver, method);

        if (xPortIsInsideInterrupt()) {
            BaseType_t higherPriorityTaskWoken = pdFALSE;
            if (xQueueSendFromISR(mQueue, &storage, &higherPriorityTaskWoken) == pdPASS) {
                portYIELD_FROM_ISR(higherPriorityTaskWoken);
            }
        }
        else {
            xQueueSend(mQueue, &storage, 10);
        }
    }

private:
    static constexpr UBaseType_t TASK_PRIORITY = 24; // osPriorityNormal
    static constexpr uint16_t STACK_SIZE_W = 1024;

    static inline uint8_t mQueueStorage[QUEUE_SIZE * sizeof(EventStorage)];
    static inline StaticQueue_t mQueueStruct;
    static inline QueueHandle_t mQueue = nullptr;

    static inline StackType_t mTaskStack[STACK_SIZE_W];
    static inline StaticTask_t mTaskStruct;
    static inline TaskHandle_t mTaskHandle = nullptr;

    static void taskHandler(void *param)
    {
        while (true) {
            EventStorage storage;

            if (xQueueReceive(mQueue, &storage, portMAX_DELAY)) {
                EventBase* event = reinterpret_cast<EventBase*>(storage.data);
                event->call();
                event->~EventBase();
            }
        }
    }
};



// Static constructor for Dispatcher service
//__attribute__((constructor))
//inline void InitDispatcher()
//{
//    Observer::Dispatcher::init();
//}

/**
 * @brief   Observers holder template class for storing observers and calling
 *          methods.
 * @tparam  T: The type of the observer objects.
 * @tparam  N: The maximum number of observers that can be stored.
 *
 * @note    Features and limitations:
 *          - Dynamic memory is not used, the user must specify the maximum
 *            number and type of observers when defining.
 *          - Pointers to observers may remain invalid if the ConcreteObserver
 *            has been removed. It is the user's responsibility to use the
 *            del() method to remove the observer from the list.
 *          - Requires at least C++17.
 *          - Not thread-safe.
 *
 * @verbatim
 *   // Example:
 *
 *   // Callbacks class with virtual functions
 *    class Callbacks {
 *    public:
 *        virtual void foo(uint32_t param1) = 0;
 *        virtual void bar(uint32_t param1, float param2) = 0;
 *        virtual ~Callbacks() = default;
 *    };
 *
 *    // A concrete observer class
 *    class ConcreteObserver : public Callbacks {
 *    public:
 *        void foo(uint32_t param1) override {
 *            printf("%d\n", param1);
 *        }
 *
 *        void bar(uint32_t param1, float param2) override {
 *             printf("%d, %f\n", param1, param2);
 *        }
 *    };
 *
 *   // The Subject class that raises events
 *   class ConcreteSubject {
 *   public:
 *      // A method to add an observer
 *      void attach(Callbacks *pObserver) {
 *          mHolder.add(pObserver);
 *      }
 *
 *   void event1() {
 *      mHolder.notify(&Callbacks::foo, 5);
 *   }
 *
 *   void event2() {
 *       mHolder.notify(&Callbacks::bar, 3, 47,8f);
 *   }
 *
 *   private:
 *      Observer::Holder<Callbacks, 2> mHolder {};
 *   };
 *
 *   int main() {
 *      ConcreteSubject subject;
 *
 *      ConcreteObserver observer1;
 *      ConcreteObserver observer2;
 *
 *      // Add observers
 *      subject.attach(&observer1);
 *      subject.attach(&observer2);
 *
 *      // Call events
 *      subject.event1(); // Calls foo for all observers
 *      subject.event2(); // Calls bar for all observers
 *
 *      return 0;
 *    }
 *
 * @endverbatim
 */
template <typename T, std::size_t N>
class Holder {

public:
    // TODO: add a mutex

    /**
     * @brief   Constructor.
     */
    Holder() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Holder() = default;

    /**
     * @brief   Adds a new observer to the holder.
     * @param   pObserver: Pointer to the observer to be added.
     * @retval  'true' if the observer was added successfully,
     *          'false' if the holder is full.
     */
    bool add(T *pObserver)
    {
        for (uint32_t i = 0; i < N; i++) {
            if (mObservers[i] == nullptr) {
                mObservers[i] = pObserver;
                return true;
            }
        }
        // No free space
        return false;
    }

    /**
     * @brief   Removes an observer from the holder.
     * @param   pObserver: Pointer to the observer to be removed.
     */
    void del(T *pObserver)
    {
        for (uint32_t i = 0; i < N; i++) {
            if (mObservers[i] == pObserver) {
                mObservers[i] = nullptr;
                return;
            }
        }
    }

    /**
     * @brief   Notifies all registered observers by invoking a specified member
     *          function with provided arguments.
     * @tparam  Func: Type of the member function to be invoked.
     * @tparam  Args: Types of the arguments to be passed to the member function.
     * @param   func: Member function to be invoked on each observer.
     * @param   args: Arguments to be forwarded to the member function.
     *
     * This method iterates over all registered observers and invokes the specified member function
     * on each observer with the provided arguments. The function uses perfect forwarding to pass
     * the arguments to the member function.
     */
    template<typename Func, typename ... Args>
    void notify(Func &&func, Args &&... args)
    {
        for (uint32_t i = 0; i < N; i++) {
            T *pObserver = mObservers[i];
            if (pObserver) {
                std::invoke(func, pObserver, std::forward<Args>(args)...);
            }
        }
    }

    // Without arguments only
    template<typename ObserverType>
    void notifyISR(void (ObserverType::*method)())
    {
        for (uint32_t i = 0; i < N; i++) {
            T *pObserver = mObservers[i];
            if (pObserver) {
                ObserverType *typedObserver = static_cast<ObserverType*>(pObserver);
                Dispatcher::dispatch(typedObserver, method);
            }
        }
    }

private:
    // Array to store pointers to observer objects.
    T *mObservers[N] {};
};

} /* namespace Observer */

#endif /* __OBSERVER_HPP */
