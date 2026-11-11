/**
 ******************************************************************************
 * @file    ProdTestUSB.hpp
 * @brief   Production test for the USB subsystem.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "ProdTest/LCD/TestLCD.hpp"
#include "UsbProtection.hpp"
#include "IFSM.hpp"

namespace ProdTest {

class IUsbProdTestFSMPort {
public:
    enum class State {
        DEFAULT = 0,
        CONNECTED,
        DAM,
        ERROR
    };

    enum class Event {
        NONE = 0,
    };

    enum class Flag {
        UNKNOWN = 0,
    };

    virtual bool x0() = 0;  //// Fault
    virtual bool x1() = 0;  //// Sink
    virtual bool x2() = 0;  //// DAM

    virtual void z0() = 0;  //// Fill the LCD in black
    virtual void z1() = 0;  //// Fill the LCD in green
    virtual void z2() = 0;  //// Fill the LCD in blue
    virtual void z3() = 0;  //// Fill the LCD in red
};

class UsbProdTestFSM : public Interface::IFSM<IUsbProdTestFSMPort::State,
                                              IUsbProdTestFSMPort::Event,
                                              IUsbProdTestFSMPort::Flag>
{
public:
    explicit UsbProdTestFSM(IUsbProdTestFSMPort& port);
    virtual ~UsbProdTestFSM() {}

private:
    void processing(IUsbProdTestFSMPort::Event e) override;
    void processingNewState() override;

    IUsbProdTestFSMPort& mPort;
};

class USB : public Interface::IProdTest,
                   IUsbProdTestFSMPort {
public:
    USB(const std::string& id);
    ~USB() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

    bool x0() override;  //// Fault
    bool x1() override;  //// Sink
    bool x2() override;  //// DAM

    void z0() override;  //// Fill the LCD in black
    void z1() override;  //// Fill the LCD in green
    void z2() override;  //// Fill the LCD in blue
    void z3() override;  //// Fill the LCD in red

private:
    bool checkIC(StringLogger& slog);

    void cmdSetDAM(int argc, const char** argv);
    void cmdShowStatus();
    void cmdLog();
    void cmdShowUsbFaultLineLevel();
    void cmdShowStatusOnLCD();
    void checkCommand(CLIInput_t* input);

    UsbProdTestFSM    mFSM;
    ProdTest::TestLCD mLCD;
};

}   // namespace ProdTest
