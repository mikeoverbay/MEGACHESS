#include "panel_sim.h"

// The real classic font table from Adafruit_GFX, so text matches the panel.
#include "glcdfont.c"

#include <deque>
#include <utility>

// A tap is held for HOLD polls, then the panel reads "released" for GAP
// polls before the next queued tap starts. HOLD comfortably covers get_tap's
// 45ms sampling window; GAP clears both wait_release() and the 240ms debounce,
// so consecutive queued taps each register.
static std::deque<std::pair<int, int>> tapQueue;
static int hold = 0, gap = 0, curX = -1, curY = -1;
static const int HOLD = 40, GAP = 150;   // GAP x 2ms must exceed get_tap's 240ms debounce

void SimPanel::queueTap(int x, int y) { tapQueue.push_back(std::make_pair(x, y)); }
bool SimPanel::busy() { return !tapQueue.empty() || hold > 0 || gap > 0; }

SimPanel::SimPanel() { memset(fb, 0, sizeof fb); }

void SimPanel::begin() { fillScreen(0); }

// --- touch -------------------------------------------------------------------
bool SimPanel::getTouch(int& x, int& y) {
    sim_advance(2);
    if (hold > 0) {
        x = curX; y = curY;
        if (--hold == 0) { curX = curY = -1; gap = GAP; }
        return true;
    }
    if (gap > 0) { gap--; return false; }
    if (!tapQueue.empty()) {
        curX = tapQueue.front().first; curY = tapQueue.front().second;
        tapQueue.pop_front();
        hold = HOLD - 1;
        x = curX; y = curY;
        return true;
    }
    return false;
}

void SimPanel::readTouchRaw(int& x, int& y, int& z) {
    sim_advance(1);
    x = 500; y = 500;
    z = (hold > 0) ? 300 : 0;
}

// --- primitives --------------------------------------------------------------
void SimPanel::fillScreen(uint16_t c) {
    for (int32_t i = 0; i < (int32_t) W * H; i++) fb[i] = c;
}

void SimPanel::drawPixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[(int32_t) y * W + x] = c;
}

void SimPanel::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    for (int16_t j = 0; j < h; j++)
        for (int16_t i = 0; i < w; i++)
            drawPixel(x + i, y + j, c);
}

void SimPanel::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    drawFastHLine(x, y, w, c);
    drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c);
    drawFastVLine(x + w - 1, y, h, c);
}

void SimPanel::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, c);
}

void SimPanel::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    for (int16_t j = 0; j < h; j++) drawPixel(x, y + j, c);
}

// Adafruit_GFX::drawCircle, verbatim algorithm.
void SimPanel::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    drawPixel(x0, y0 + r, c); drawPixel(x0, y0 - r, c);
    drawPixel(x0 + r, y0, c); drawPixel(x0 - r, y0, c);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        drawPixel(x0 + x, y0 + y, c); drawPixel(x0 - x, y0 + y, c);
        drawPixel(x0 + x, y0 - y, c); drawPixel(x0 - x, y0 - y, c);
        drawPixel(x0 + y, y0 + x, c); drawPixel(x0 - y, y0 + x, c);
        drawPixel(x0 + y, y0 - x, c); drawPixel(x0 - y, y0 - x, c);
    }
}

// Adafruit_GFX::fillCircleHelper, verbatim algorithm.
void SimPanel::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t c) {
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
    delta++;
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (x < (y + 1)) {
            if (corners & 1) drawFastVLine(x0 + x, y0 - y, 2 * y + delta, c);
            if (corners & 2) drawFastVLine(x0 - x, y0 - y, 2 * y + delta, c);
        }
        if (y != py) {
            if (corners & 1) drawFastVLine(x0 + py, y0 - px, 2 * px + delta, c);
            if (corners & 2) drawFastVLine(x0 - py, y0 - px, 2 * px + delta, c);
            py = y;
        }
        px = x;
    }
}

void SimPanel::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
    drawFastVLine(x0, y0 - r, 2 * r + 1, c);
    fillCircleHelper(x0, y0, r, 3, 0, c);
}

// Adafruit_GFX::drawBitmap (PROGMEM, no background), verbatim algorithm.
void SimPanel::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t c) {
    int16_t byteWidth = (w + 7) / 8;
    uint8_t b = 0;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (i & 7) b <<= 1; else b = bitmap[j * byteWidth + i / 8];
            if (b & 0x80) drawPixel(x + i, y + j, c);
        }
    }
}

// --- text: Adafruit's classic 5x7 font rules ---------------------------------
void SimPanel::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bgc, uint8_t size) {
    if (x >= W || y >= H || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0) return;
    if (c >= 176) c++;                     // classic charset quirk, cp437 off
    for (int8_t i = 0; i < 5; i++) {
        uint8_t line = font[c * 5 + i];
        for (int8_t j = 0; j < 8; j++, line >>= 1) {
            if (line & 1) {
                if (size == 1) drawPixel(x + i, y + j, color);
                else fillRect(x + i * size, y + j * size, size, size, color);
            } else if (bgc != color) {
                if (size == 1) drawPixel(x + i, y + j, bgc);
                else fillRect(x + i * size, y + j * size, size, size, bgc);
            }
        }
    }
    if (bgc != color) {                    // the 6th, spacing column
        if (size == 1) drawFastVLine(x + 5, y, 8, bgc);
        else fillRect(x + 5 * size, y, size, 8 * size, bgc);
    }
}

size_t SimPanel::write(uint8_t c) {
    if (c == '\n') { cx = 0; cy += tsize * 8; }
    else if (c != '\r') {
        if (wrap && (cx + tsize * 6) > W) { cx = 0; cy += tsize * 8; }
        drawChar(cx, cy, c, fg, bg, tsize);
        cx += tsize * 6;
    }
    return 1;
}

// --- streaming writes (piece blits, splash) ----------------------------------
void SimPanel::setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    ax0 = x0; ay0 = y0; ax1 = x1; ay1 = y1; apx = x0; apy = y0;
}

void SimPanel::pushColors(uint16_t* data, uint32_t len) {
    while (len--) {
        drawPixel(apx, apy, *data++);
        if (++apx > ax1) { apx = ax0; if (++apy > ay1) apy = ay0; }
    }
}

// --- output ------------------------------------------------------------------
bool SimPanel::dump(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int32_t i = 0; i < (int32_t) W * H; i++) {
        const uint16_t p = fb[i];
        const uint8_t rgb[3] = {
            (uint8_t) (((p >> 11) & 0x1F) * 255 / 31),
            (uint8_t) (((p >> 5)  & 0x3F) * 255 / 63),
            (uint8_t) ((p & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}
