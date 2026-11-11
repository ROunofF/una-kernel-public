#ifndef QRCODEVIEW_HPP
#define QRCODEVIEW_HPP

#include <gui_generated/qrcode_screen/QRCodeViewBase.hpp>
#include <gui/qrcode_screen/QRCodePresenter.hpp>

class QRCodeView : public QRCodeViewBase
{
public:
    QRCodeView();
    virtual ~QRCodeView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setInfo(const DeviceInfo& devInfo);

protected:
    void update();

    virtual void handleKeyEvent(uint8_t key) override;
    touchgfx::BitmapId           mQrBmpId = touchgfx::BITMAP_INVALID;
    touchgfx::Bitmap::BitmapData mQrBmpData{};
    DeviceInfo                   mDevInfo{};
    uint8_t*                     mBuffer{};
};

#endif // QRCODEVIEW_HPP
