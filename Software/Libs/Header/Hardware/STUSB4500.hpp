/**
 ******************************************************************************
 * @file    STUSB4500.hpp
 * @brief   USB Type-C / USB-PD sink controller.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_STUSB4500_HPP
#define HARDWARE_STUSB4500_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

namespace Hardware
{

class STUSB4500
{

public:

    // I2C address
    static const uint8_t skI2cAddr = 0x29;

    /**
     * @brief   Register addresses.
     */
    enum class Regs : uint8_t {
        ALERT_STATUS_1            = 0x0B,
        ALERT_STATUS_1_MASK       = 0x0C,
        PORT_STATUS_0             = 0x0D,
        PORT_STATUS_1             = 0x0E,
        TYPEC_MONITORING_STATUS_0 = 0x0F,
        TYPEC_MONITORING_STATUS_1 = 0x10,
        CC_STATUS                 = 0x11,
        CC_HW_FAULT_STATUS_0      = 0x12,
        CC_HW_FAULT_STATUS_1      = 0x13,
        TYPEC_STATUS              = 0x15,
        PRT_STATUS                = 0x16,

        RESET_CTRL                = 0x23,
        DEVICE_ID                 = 0x2F,
    };

    /**
     * @brief   ALERT_STATUS_1 register layout. Used both for the latch
     *          and the mask register (same bit positions).
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t RESERVED_0                 : 1; // bit 0
            uint8_t PRT_STATUS_AL              : 1; // bit 1
            uint8_t RESERVED_2                 : 1; // bit 2
            uint8_t RESERVED_3                 : 1; // bit 3
            uint8_t CC_HW_FAULT_STATUS_AL      : 1; // bit 4
            uint8_t TYPEC_MONITORING_STATUS_AL : 1; // bit 5
            uint8_t PORT_STATUS_AL             : 1; // bit 6
            uint8_t RESERVED_7                 : 1; // bit 7
        } b;
    } ALERT_STATUS_1;

    using ALERT_STATUS_1_MASK = ALERT_STATUS_1;

    /**
     * @brief   Convenience bit masks for ALERT_STATUS_1.
     */
    struct AlertStatus1Bits {
        static constexpr uint8_t PRT_STATUS_AL              = 1u << 1;
        static constexpr uint8_t CC_HW_FAULT_STATUS_AL      = 1u << 4;
        static constexpr uint8_t TYPEC_MONITORING_STATUS_AL = 1u << 5;
        static constexpr uint8_t PORT_STATUS_AL             = 1u << 6;
    };

    /**
     * @brief   RESET_CTRL register bit values.
     */
    struct ResetCtrlBits {
        static constexpr uint8_t RESET_SW_EN  = 0x01;
        static constexpr uint8_t RESET_SW_DIS = 0x00;
    };

    /**
     * @brief   PORT_STATUS_0 register layout (transitions).
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t ATTACH_TRANS : 1;
            uint8_t RESERVED     : 7;
        } b;
    } PORT_STATUS_0;

    /**
     * @brief   PORT_STATUS_1 register layout (steady state).
     *          ATTACHED_DEVICE values are listed in PortStatusAttachedDevice.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t ATTACH          : 1;
            uint8_t RESERVED_1      : 1;
            uint8_t DATA_MODE       : 1;
            uint8_t POWER_MODE      : 1;
            uint8_t RESERVED_4      : 1;
            uint8_t ATTACHED_DEVICE : 3;
        } b;
    } PORT_STATUS_1;

    /**
     * @brief   ATTACHED_DEVICE field codes (PORT_STATUS_1.b.ATTACHED_DEVICE).
     */
    struct PortStatusAttachedDevice {
        static constexpr uint8_t NONE_ATT = 0x00;
        static constexpr uint8_t SNK_ATT  = 0x02;
        static constexpr uint8_t DBG_ATT  = 0x03;
    };

    /**
     * @brief   TYPEC_MONITORING_STATUS_0 register layout.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t RESERVED_0           : 1;
            uint8_t VBUS_VALID_SNK_TRANS : 1;
            uint8_t VBUS_VSAFE0V_TRANS   : 1;
            uint8_t VBUS_READY_TRANS     : 1;
            uint8_t VBUS_LOW_STATUS      : 1;
            uint8_t VBUS_HIGH_STATUS     : 1;
            uint8_t RESERVED_6           : 1;
            uint8_t RESERVED_7           : 1;
        } b;
    } TYPEC_MONITORING_STATUS_0;

    /**
     * @brief   TYPEC_MONITORING_STATUS_1 register layout.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t RESERVED_0     : 1;
            uint8_t VBUS_VALID_SNK : 1;
            uint8_t VBUS_VSAFE0V   : 1;
            uint8_t VBUS_READY     : 1;
            uint8_t RESERVED_4     : 1;
            uint8_t RESERVED_5     : 1;
            uint8_t RESERVED_6     : 1;
            uint8_t RESERVED_7     : 1;
        } b;
    } TYPEC_MONITORING_STATUS_1;

    /**
     * @brief   CC_STATUS register layout.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t CC1_STATE          : 2;
            uint8_t CC2_STATE          : 2;
            uint8_t CONNECT_RESULT     : 1;
            uint8_t LOOKING4CONNECTION : 1;
            uint8_t RESERVED_6         : 1;
            uint8_t RESERVED_7         : 1;
        } b;
    } CC_STATUS;

    /**
     * @brief   CC_HW_FAULT_STATUS_0 register layout (transitions).
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t RESERVED_0          : 1;
            uint8_t RESERVED_1          : 1;
            uint8_t RESERVED_2          : 1;
            uint8_t RESERVED_3          : 1;
            uint8_t VPU_VALID_TRANS     : 1;
            uint8_t VPU_OVP_FAULT_TRANS : 1;
            uint8_t RESERVED_6          : 1;
            uint8_t RESERVED_7          : 1;
        } b;
    } CC_HW_FAULT_STATUS_0;

    /**
     * @brief   CC_HW_FAULT_STATUS_1 register layout (steady state).
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t RESERVED_0       : 1;
            uint8_t RESERVED_1       : 1;
            uint8_t RESERVED_2       : 1;
            uint8_t RESERVED_3       : 1;
            uint8_t VBUS_DISCH_FAULT : 1;
            uint8_t RESERVED_5       : 1;
            uint8_t VPU_VALID        : 1;
            uint8_t VPU_OVP_FAULT    : 1;
        } b;
    } CC_HW_FAULT_STATUS_1;

    /**
     * @brief   PRT_STATUS register layout.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t PRL_HW_RST_RECEIVED : 1;
            uint8_t RESERVED_1          : 1;
            uint8_t PRL_MSG_RECEIVED    : 1;
            uint8_t RESERVED_3          : 1;
            uint8_t PRT_BIST_RECEIVED   : 1;
            uint8_t RESERVED_5          : 1;
            uint8_t RESERVED_6          : 1;
            uint8_t RESERVED_7          : 1;
        } b;
    } PRT_STATUS;

    /**
     * @brief   TYPEC_STATUS register layout.
     */
    typedef union {
        uint8_t d8;
        struct {
            uint8_t TYPEC_FSM_STATE : 5;
            uint8_t RESERVED_5      : 1;
            uint8_t RESERVED_6      : 1;
            uint8_t RESERVED_7      : 1;
        } b;
    } TYPEC_STATUS;

    /**
     * @brief   Snapshot of the ALERT register filtered by the current mask.
     */
    struct Alerts {
        ALERT_STATUS_1      status { };
        ALERT_STATUS_1_MASK mask   { };
    };

    /**
     * @brief   Snapshot of the chip status registers.
     */
    struct Status {
        PORT_STATUS_0             portStatus0;
        PORT_STATUS_1             portStatus1;
        TYPEC_MONITORING_STATUS_0 typecMonitoringStatus0;
        TYPEC_MONITORING_STATUS_1 typecMonitoringStatus1;
        CC_HW_FAULT_STATUS_0      ccHWFaultStatus0;
        CC_HW_FAULT_STATUS_1      ccHWFaultStatus1;
        PRT_STATUS                prtStatus;
        CC_STATUS                 ccStatus;
    };

    /**
     * @brief   Constructor.
     * @param   i2c: I2C bus the chip is connected to.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     */
    STUSB4500(Interface::II2c& i2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Destructor.
     */
    virtual ~STUSB4500() = default;

    /**
     * @brief   Fast presence check via I2C device-ready probe.
     *          Used to detect whether the chip is powered and on the bus
     *          before issuing a register access; on a normal USB unplug
     *          STUSB loses VBUS and stops ACKing within milliseconds.
     * @retval  'true' if the chip ACKs its address, 'false' otherwise.
     */
    bool isReady();

    /**
     * @brief   Full identification: probe the chip and verify DEVICE_ID.
     *          Uses skI2cMaxAttempts internally. Stronger guarantee than
     *          isReady(): confirms that the device at the configured
     *          address is really a STUSB4500, not an unrelated slave.
     * @retval  'true' if the chip responds with the expected DEVICE_ID,
     *          'false' otherwise.
     */
    bool isPresent();

    /**
     * @brief   Configure ALERT_STATUS_1_MASK and drain pending alerts.
     * @param   mask: bits set to '1' are UNMASKED (alerts will fire),
     *                bits set to '0' are masked.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init(uint8_t mask);

    /**
     * @brief   Read ALERT_STATUS_1 (clear-on-read) and the current MASK,
     *          then keep only currently-unmasked bits in alerts.status.
     * @param   alerts: output snapshot.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readAlerts(Alerts& alerts);

    /**
     * @brief   Read all status registers exhaustively.
     * @param   status: output snapshot.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readStatus(Status& status);

    /**
     * @brief   Read only the status registers whose corresponding alert
     *          bit is set in @p alerts. Fields outside the alert set are
     *          left untouched, so callers must pass the previous cached
     *          value as basis.
     * @param   status: input/output snapshot.
     * @param   alerts: alerts captured by readAlerts().
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readStatus(Status& status, const Alerts& alerts);

    /**
     * @brief   Soft reset the PD state machine and drain residual alerts.
     *          I2C interface stays accessible across the reset; chip
     *          defaults are reloaded from NVM (TLOAD).
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool swResetByReg();

private:

    // Total I2C attempts per readReg / writeReg call (first attempt + retries).
    static const uint8_t skI2cMaxAttempts = 3;

    bool readReg(Regs reg, uint8_t* buff, uint8_t len);
    bool readReg(Regs reg, uint8_t& value);

    bool writeReg(Regs reg, uint8_t* buff, uint8_t len);
    bool writeReg(Regs reg, uint8_t value);

    Interface::II2c& mI2c;
    uint8_t          mDevAddr;
};

} /* namespace Hardware */

#endif /* HARDWARE_STUSB4500_HPP */
