#pragma once

#include <stdint.h>
#include <array>
#include <functional>

namespace comb_parser::charset {

class charset : public std::function<bool(uint8_t)> {
public:

  bool operator()(uint8_t c) {
    return (bitmap[(c) >> 6] & ( 1 << ((c) & 0x3F))) != 0;
  }

  charset() { }

  charset(const std::function<bool(uint8_t)>& c) {
    for (int idx = 0; idx < 256; ++idx) {
      if(c(static_cast<unsigned char>(idx))) {
        bitmap[idx >> 6] |= 1 << (idx & 0x3F);
      }
    }
  }

  charset(const std::string& s) {
    for (auto c : s) {
      bitmap[c >> 6] |= 1 << (c & 0x3F);
    }
  }

  template<typename T>
  charset operator+ (const T t) const {
    charset cs;
    charset c{t};
    cs.bitmap[0] = bitmap[0] | c.bitmap[0];
    cs.bitmap[1] = bitmap[1] | c.bitmap[1];
    cs.bitmap[2] = bitmap[2] | c.bitmap[2];
    cs.bitmap[3] = bitmap[3] | c.bitmap[3];
    return cs;
  }

  template<typename T>
  charset operator- (const T t) const {
    charset cs;
    charset c{t};
    cs.bitmap[0] = bitmap[0] & ~c.bitmap[0];
    cs.bitmap[1] = bitmap[1] & ~c.bitmap[1];
    cs.bitmap[2] = bitmap[2] & ~c.bitmap[2];
    cs.bitmap[3] = bitmap[3] & ~c.bitmap[3];
    return cs;
  }

  charset operator!() const {
    charset cs;
    cs.bitmap[0] = ~bitmap[0];
    cs.bitmap[1] = ~bitmap[1];
    cs.bitmap[2] = ~bitmap[2];
    cs.bitmap[3] = ~bitmap[3];
    return cs;
  }

private:
  std::array<uint64_t, 4> bitmap = {0,};

};

} // namespace comb_parser::charset
