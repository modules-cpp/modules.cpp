// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.audio:sample;

export namespace mm::audio {

// The only place a bit width appears in this module, and it is an argument:
// which width a transport has is the board's fact. Every function answers zero
// for bits outside [1, 32].

// A signed value of bits width to a sample: shifted up when narrower than
// sixteen bits, the upper sixteen kept when wider. A value outside the signed
// range of bits saturates.
[[nodiscard]] std::int16_t to_sample(std::int32_t value, unsigned int bits);

// An unsigned count of an offset-binary converter -- a SAR ADC -- to a sample,
// half of full scale being zero. A count above full scale saturates.
[[nodiscard]] std::int16_t to_sample_offset(std::uint32_t count, unsigned int bits);

// The inverses, for a transport that takes a signed word or an unsigned level
// of bits width: widened by shifting, narrowed by keeping the upper bits.
[[nodiscard]] std::int32_t from_sample(std::int16_t sample, unsigned int bits);
[[nodiscard]] std::uint32_t from_sample_offset(std::int16_t sample, unsigned int bits);

}
