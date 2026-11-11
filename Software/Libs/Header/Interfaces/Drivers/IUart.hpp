/**
 ******************************************************************************
 * @file    IUart.hpp
 * @brief   UART bus interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_I_UART_HPP
#define INTERFACE_I_UART_HPP

#include <cstdint>
#include <cstddef>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   UART bus interface.
 */
class IUart
{

public:

    /**
     * @brief   Initialization of the UART instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialization of the UART instance.
     */
    virtual void deinit() = 0;

    /**
     * @brief   Optional method to start receiving in non-blocking mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool startReception() { return true; };

    /**
     * @brief   Optional method for stopping reception in non-blocking mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool stopReception() { return true; };

    /**
     * @brief   Optional method for getting reception state.
     * @retval  Reception status. 'true' - active, 'false' - inactive.
     */
    virtual bool isReceptionActive() { return false; };

    /**
     * @brief   Transmit data.
     * @param   pData: pointer to transmit buffer.
     * @param   size: data size.
     * @param   timeout:  > 0: blocking mode (0xFFFFFFFF: infinity timeout),
     *          0: non-blocking mode (interrupt/DMA).
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    virtual bool transmit(const uint8_t *pData, size_t size,
            uint32_t timeout = UINT32_MAX) = 0;

    /**
     * @brief Check if transmission in progress.
     * @retval  'true' - transmission in progress, 'false' - ready to transmit.
     */
    virtual bool isTxBusy() = 0;

    /**
     * @brief   Receives data.
     * @param   pData: Pointer to data buffer.
     * @param   size: The maximum amount of data to receive.
     * @param   br: the number of bytes actually received.
     * @param   timeout:  > 0: blocking mode (0xFFFFFFFF: infinity timeout),
     *          0: non-blocking mode (interrupt/DMA).
     * @retval  'true' - received at least 1 byte, 'false' - no data received
     *          or any error.
     */
    virtual bool receive(uint8_t *pData, size_t size, size_t &br,
            uint32_t timeout = UINT32_MAX) = 0;

    /**
     * @brief   Get the number of bytes available for reading.
     * @retval  The number of bytes available for reading.
     */
    virtual size_t getAvailableDataSize() = 0;

    /**
     * @brief   Abort Rx reception.
     */
    virtual void abortRx() = 0;

    /**
     * @brief   Flush RX buffer.
     */
    virtual void flushRx() = 0;

    /**
     * @brief   This allows you to set an external buffer for receiving
     *          data, instead of an internal one.
     * @note    The buffer address must be 32-byte aligned. The buffer size
     *          must be a multiple of 32 bytes.
     * @param   pBuff: Pointer to the external buffer. If the pointer is null,
     *          then the internal buffer will be used.
     * @param   size: The size of the buffer is in bytes. If the size is 0,
     *          then the internal buffer will be used.
     */
    virtual void setBuffer(void *pBuff, size_t size) = 0;

    /**
     * @brief   Set new baudrate.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool setBaudrate(uint32_t baudrate) = 0;

    /**
     * @brief   Recover the peripheral after a failed or timed-out transmit.
     *          Brings the transmitter back to a known-good idle state so the
     *          next transmit can start. Concrete drivers override this; the
     *          default does nothing (suitable for stubs/mocks).
     * @retval  'true' - recovered, 'false' - not supported / failed.
     */
    virtual bool recover() { return false; }

    /**
     * @brief   UART transfer complete interrupt handler interface.
     */
    class CpltHandler {
    public:
        /**
         * @brief   Transmit transfer complete interrupt handler.
         * @param   pUart: pointer to the IUart instance.
         * @param   status: 'true' - success, 'false' - otherwise.
         */
        virtual void transmitCplt(Interface::IUart *pUart, bool status){}

        /**
         * @brief   Receive transfer complete interrupt handler.
         * @retval  The number of bytes available for reading.
         */
        virtual void receiveCplt(Interface::IUart *pUart, size_t size){}
    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~CpltHandler() = default;
    };

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler IUart::CpltHandler.
     */
    virtual void setCpltHandler(Interface::IUart::CpltHandler *pHandler) = 0;


protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IUart() = default;

};



} // namespace Interface

#endif // INTERFACE_I_UART_HPP
