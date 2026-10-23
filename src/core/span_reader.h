#pragma once
// Bounds-checked little-endian reader over a byte span. Importers use this instead of pointer casts so that a
// malformed file produces a clean failure, never undefined behaviour.
#include <stdexcept>

#include "core/types.h"

namespace oc {

class SpanReader {
 public:
  explicit SpanReader(ByteSpan data, size_t pos = 0) : data_(data), pos_(pos) {}

  size_t pos() const { return pos_; }
  size_t size() const { return data_.size(); }
  size_t remaining() const { return pos_ <= data_.size() ? data_.size() - pos_ : 0; }
  bool eof() const { return pos_ >= data_.size(); }
  ByteSpan data() const { return data_; }

  void seek(size_t pos) {
    if (pos > data_.size()) throw std::out_of_range("SpanReader::seek past end");
    pos_ = pos;
  }
  void skip(size_t count) { seek(pos_ + count); }

  u8 u8_() { return read<u8>(); }
  u16 u16_() { return read<u16>(); }
  u32 u32_() { return read<u32>(); }
  s8 s8_() { return static_cast<s8>(read<u8>()); }
  s16 s16_() { return static_cast<s16>(read<u16>()); }
  s32 s32_() { return static_cast<s32>(read<u32>()); }

  u8 peek_u8(size_t off = 0) const { return at<u8>(pos_ + off); }
  u16 peek_u16(size_t off = 0) const { return at<u16>(pos_ + off); }
  u32 peek_u32(size_t off = 0) const { return at<u32>(pos_ + off); }

  ByteSpan bytes(size_t count) {
    if (count > remaining()) throw std::out_of_range("SpanReader::bytes past end");
    ByteSpan out = data_.subspan(pos_, count);
    pos_ += count;
    return out;
  }
  ByteSpan slice(size_t off, size_t count) const {
    if (off > data_.size() || count > data_.size() - off) throw std::out_of_range("SpanReader::slice out of range");
    return data_.subspan(off, count);
  }

  // Little-endian value at an absolute offset.
  template <typename T>
  T at(size_t off) const {
    if (off > data_.size() || sizeof(T) > data_.size() - off) throw std::out_of_range("SpanReader::at out of range");
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) value |= static_cast<T>(data_[off + i]) << (8 * i);
    return value;
  }

 private:
  template <typename T>
  T read() {
    T value = at<T>(pos_);
    pos_ += sizeof(T);
    return value;
  }

  ByteSpan data_;
  size_t pos_;
};

}  // namespace oc
