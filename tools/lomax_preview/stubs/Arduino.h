// Just enough of Arduino for TFT_eSPI's sprites to compile on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <algorithm>
#include <string>
using std::min;
using std::max;
typedef bool boolean;
typedef uint8_t byte;
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_ptr(a) (*(void *const *)(a))
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define MSBFIRST 1
#define SPI_MODE0 0
#define DEC 10
#define HEX 16
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned) {}
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline void yield() {}
inline uint32_t digitalPinToBitMask(int) { return 0; }
inline long random(long n) { return n > 0 ? rand() % n : 0; }
inline char *ltoa(long v, char *s, int) { sprintf(s, "%ld", v); return s; }
inline void *ps_malloc(size_t n) { return malloc(n); }
inline void *ps_calloc(size_t n, size_t s) { return calloc(n, s); }

class String {
 public:
  std::string s;
  String(const char *c = "") : s(c) {}
  String(const std::string &x) : s(x) {}
  String(int v) : s(std::to_string(v)) {}
  const char *c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); }
  char charAt(unsigned i) const { return s[i]; }
  char operator[](unsigned i) const { return s[i]; }
  void toCharArray(char *buf, unsigned n) const { strncpy(buf, s.c_str(), n); if (n) buf[n - 1] = 0; }
  String operator+(const String &o) const { return String(s + o.s); }
  bool operator==(const char *o) const { return s == o; }
  bool endsWith(const String &o) const { return s.size() >= o.s.size() && s.compare(s.size() - o.s.size(), o.s.size(), o.s) == 0; }
  bool startsWith(const String &o) const { return s.compare(0, o.s.size(), o.s) == 0; }
};

class __FlashStringHelper;
#define F(s) (s)

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t print(const char *s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
  size_t print(const String &s) { return print(s.c_str()); }
  size_t println(const char *s = "") { return print(s) + write('\n'); }
  size_t printf(const char *fmt, ...) {
    char b[256];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return print(b);
  }
};

class HardwareSerial : public Print {
 public:
  size_t write(uint8_t c) override { return fputc(c, stderr) != EOF; }
};
extern HardwareSerial Serial;
