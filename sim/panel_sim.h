// Megachess simulator - the display.
//
// A 480x320 RGB565 framebuffer behind the subset of the Adafruit_GFX /
// DIYables API that ui.cpp and storage.cpp use. Text uses the real glcdfont
// table and Adafruit's drawChar rules; circles use Adafruit's midpoint code;
// so what lands in the buffer is what the panel would show, pixel for pixel.
//
// Touch is a queue of taps. Each tap is held for a run of polls and then
// released for a run of polls, so the sketch sees press, hold, lift - which
// matters for anything that waits for a release, like the confirm dialog.
// Every poll advances simulated time a little so sampling windows terminate.
#pragma once
#include <Arduino.h>

class SimPanel : public Print {
public:
    static const int16_t W = 480, H = 320;
    uint16_t fb[W * H];

    static void queueTap(int x, int y);
    static bool busy();                   // a tap is pending or in progress

    SimPanel();

    void begin();
    void setRotation(uint8_t r) { rot = r; }
    uint8_t getRotation() const { return rot; }
    int16_t width() const  { return W; }
    int16_t height() const { return H; }

    void setTouchCalibration(int a, int b, int c, int d) { cal[0] = a; cal[1] = b; cal[2] = c; cal[3] = d; }
    bool getTouch(int& x, int& y);
    void readTouchRaw(int& x, int& y, int& z);

    void fillScreen(uint16_t c);
    void drawPixel(int16_t x, int16_t y, uint16_t c);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c);
    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c);
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c);
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t c);

    void setTextSize(uint8_t s) { tsize = s ? s : 1; }
    void setTextColor(uint16_t c) { fg = bg = c; }
    void setTextColor(uint16_t c, uint16_t b) { fg = c; bg = b; }
    void setCursor(int16_t x, int16_t y) { cx = x; cy = y; }
    size_t write(uint8_t c) override;
    using Print::write;

    void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void pushColors(uint16_t* data, uint32_t len);

    bool dump(const char* path);          // binary PPM

protected:
    void writeCommand(uint8_t) {}
    void writeData(uint8_t) {}

private:
    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bgc, uint8_t size);
    void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t c);

    int16_t  cx = 0, cy = 0;
    uint16_t fg = 0xFFFF, bg = 0xFFFF;
    uint8_t  tsize = 1;
    uint8_t  rot = 0;
    bool     wrap = true;
    int      cal[4] = { 136, 907, 942, 139 };
    int16_t  ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0, apx = 0, apy = 0;
};
