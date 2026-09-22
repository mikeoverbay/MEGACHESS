// Megachess simulator - stand-in for Arduino.h.
//
// Enough of the Arduino core for the sketch, ui.cpp, storage.cpp and the
// MicroChess engine to compile natively. Time is SIMULATED: millis() only
// advances through delay() and the panel's touch polls, so every wait in the
// sketch resolves instantly and the run is deterministic.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <climits>

typedef uint8_t byte;
typedef bool    boolean;
typedef uint16_t word;
// Arduino defines word() as a macro over makeWord() so `word` still works as
// a type name when no parentheses follow it.
inline uint16_t makeWord(uint16_t w) { return w; }
inline uint16_t makeWord(uint8_t h, uint8_t l) { return (uint16_t) ((h << 8) | l); }
#define word(...) makeWord(__VA_ARGS__)

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#define PSTR(s) (s)
enum { A0 = 54, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15 };

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

// PROGMEM reads are plain memory here. The template returns the pointee at its
// natural width, so pgm_read_word(&ptrTable[i]) yields a whole 64-bit pointer
// rather than the truncated 16 bits an AVR would read.
template <class T> inline T pgm_read_any(const T* p) { return *p; }
#define pgm_read_byte(a)      pgm_read_any(a)
#define pgm_read_word(a)      pgm_read_any(a)
#define pgm_read_dword(a)     pgm_read_any(a)
#define pgm_read_ptr(a)       pgm_read_any(a)
#define pgm_read_byte_far(a)  (*(const uint8_t*) (uintptr_t) (a))
#define pgm_read_word_far(a)  (*(const uint16_t*) (uintptr_t) (a))
#define memcpy_P   memcpy
#define strncpy_P  strncpy
#define strcpy_P   strcpy
#define strlen_P   strlen
#define strcmp_P   strcmp
#define strncmp_P  strncmp
#define snprintf_P snprintf
#define sprintf_P  sprintf
#define vsnprintf_P vsnprintf

// AVR libc float formatter
inline char* dtostrf(double v, signed char width, unsigned char prec, char* out) {
    sprintf(out, "%*.*f", (int) width, (int) prec, v); return out;
}

// simulated time
uint32_t millis();
uint32_t micros();
void     delay(uint32_t ms);
void     delayMicroseconds(uint32_t us);
void     sim_advance(uint32_t ms);

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return HIGH; }
inline int  analogRead(uint8_t) { return 0; }
inline void randomSeed(uint32_t s) { srand(s); }
inline long random(long n) { return n > 0 ? rand() % n : 0; }
inline long random(long a, long b) { return a + random(b - a); }

#define bitSet(v, b)   ((v) |= (1UL << (b)))
#define bitClear(v, b) ((v) &= ~(1UL << (b)))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }

// Arduino's Print, enough for Serial, the panel and the PGN writer.
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; for (size_t i = 0; i < n; i++) k += write(b[i]); return k; }
    size_t write(const char* s) { return s ? write((const uint8_t*) s, strlen(s)) : 0; }
    size_t write(const char* b, size_t n) { return write((const uint8_t*) b, n); }

    size_t print(const char* s)                 { return write(s); }
    size_t print(const __FlashStringHelper* s)  { return write((const char*) s); }
    size_t print(char c)                        { return write((uint8_t) c); }
    size_t print(int v)                         { char b[24]; snprintf(b, sizeof b, "%d", v); return write(b); }
    size_t print(unsigned v)                    { char b[24]; snprintf(b, sizeof b, "%u", v); return write(b); }
    size_t print(long v)                        { char b[24]; snprintf(b, sizeof b, "%ld", v); return write(b); }
    size_t print(unsigned long v)               { char b[24]; snprintf(b, sizeof b, "%lu", v); return write(b); }
    size_t print(double v, int digits = 2)      { char b[32]; snprintf(b, sizeof b, "%.*f", digits, v); return write(b); }

    size_t println()                            { return write("\n"); }
    template <class T> size_t println(T v)      { size_t n = print(v); return n + println(); }
    size_t println(double v, int digits)        { size_t n = print(v, digits); return n + println(); }
};

class SerialSim : public Print {
public:
    size_t write(uint8_t c) override { fputc(c, stdout); return 1; }
    using Print::write;
    void begin(long) {}
    void end() {}
    int  available() { return 0; }
    int  read() { return -1; }
    int  availableForWrite() { return 64; }
    operator bool() const { return true; }
};
extern SerialSim Serial;
