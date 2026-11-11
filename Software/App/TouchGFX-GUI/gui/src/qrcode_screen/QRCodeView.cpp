#include <gui/qrcode_screen/QRCodeView.hpp>
#include "qrcodegen.h"

#include <string>
#include <new>   // std::nothrow

QRCodeView::QRCodeView()
{
}

void QRCodeView::setupScreen()
{
    QRCodeViewBase::setupScreen();

    // Configure button colors (not related to QR)
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);

    // Set the size of the QR image container
    ImageQR.setWidth(115);
    ImageQR.setHeight(115);
}

void QRCodeView::tearDownScreen()
{
    if (mQrBmpId != touchgfx::BITMAP_INVALID)
    {
        if (mBuffer) {
            delete[] mBuffer;
			mBuffer = nullptr;
        }

        touchgfx::Bitmap::dynamicBitmapDelete(mQrBmpId);
        mQrBmpId = touchgfx::BITMAP_INVALID;
    }

    QRCodeViewBase::tearDownScreen();
}

void QRCodeView::setInfo(const DeviceInfo& devInfo)
{
    mDevInfo = devInfo;
    update();
}

void QRCodeView::update()
{
    const char* api  = "https://go.unawatch.com/Wlsa/ed1woc4j?deep_link_value=Una%20Watch%20";
    std::string text = std::string(api) + mDevInfo.getId();

    // --- 1) Generate QR matrix ---
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];

    if (!qrcodegen_encodeText(text.c_str(),
                              tmp,
                              qr,
                              qrcodegen_Ecc_LOW,
                              4,
                              10,
                              qrcodegen_Mask_AUTO,
                              true)) {
        return; // QR generation failed
    }

    const int     qrSize = qrcodegen_getSize(qr);
    const uint8_t border = 2;

    // --- 2) Compute scaling so QR fits inside ImageQR ---
    const uint16_t W = ImageQR.getWidth();
    const uint16_t H = ImageQR.getHeight();
    const uint16_t maxSide = (W < H) ? W : H;

    uint8_t  scale = 8;
    uint16_t full;
    while (scale > 1) {
        full = (qrSize + 2 * border) * scale;

        if (full <= maxSide) {
            break;
        }

        --scale;
    }

    // --- 3) Allocate pixel buffer in heap (ABGR2222 = 1 byte per pixel) ---
    if (mBuffer == nullptr) {
        mBuffer = new (std::nothrow) uint8_t[W * H];
        if (!mBuffer) {
            return;
        }

		memset(mBuffer, 0, W * H); // initialize to black
    }

    // Compute top-left corner for centering the QR inside the bitmap
    const int ox = (int)(W - full) / 2;
    const int oy = (int)(H - full) / 2;

    // --- 4) Render QR into the buffer (ABGR2222 format) ---
    for (uint16_t y = 0; y < full; ++y)
    {
        int my = (int)(y / scale) - (int)border;
        int py = oy + (int)y;
        if ((unsigned)py >= (unsigned)H) {
            continue;
        }

        for (uint16_t x = 0; x < full; ++x) {
            int mx = (int)(x / scale) - (int)border;
            int px = ox + (int)x;
            if ((unsigned)px >= (unsigned)W) {
                continue;
            }

            bool black = false;
            if ((unsigned)mx < (unsigned)qrSize && (unsigned)my < (unsigned)qrSize) {
                black = qrcodegen_getModule(qr, mx, my);
            }

            // 0x0C = opaque black, 0xFF = opaque white in your ABGR2222 scheme
            mBuffer[py * W + px] = black ? 0x0C : 0xFF;
        }
    }

    // --- 5) Register the bitmap using external memory ---
    if (mQrBmpId == touchgfx::BITMAP_INVALID) {
        mQrBmpId = touchgfx::Bitmap::dynamicBitmapCreateExternal(W,
                                                                 H,
                                                                 mBuffer,                        // our heap buffer
                                                                 touchgfx::Bitmap::ABGR2222);   // same format as your original code

        if (mQrBmpId == touchgfx::BITMAP_INVALID) {
            delete[] mBuffer; // cleanup on failure
            mBuffer = nullptr;
            return;
        }

        ImageQR.setXY((240 - W) / 2, (240 - H) / 2);
        ImageQR.setBitmap(touchgfx::Bitmap(mQrBmpId));
    }

    // --- 6) Display the QR image ---
    ImageQR.invalidate();

    // NOTE:
    // Do NOT free(buffer) here — TouchGFX uses it directly.
    // Free it in tearDownScreen().
}

void QRCodeView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        application().gotoPairingScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoPairingCancelScreenNoTransition();
    }
}
