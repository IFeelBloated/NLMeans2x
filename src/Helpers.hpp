#pragma once
#include <cstdint>
#include <cstddef>

constexpr auto operator""_i64(unsigned long long Value) {
	return static_cast<std::int64_t>(Value);
}

template<typename T>
struct FramePointer final {
	T *Pointer = nullptr;
	decltype(0_i64) Width = 0;
	FramePointer(const void *Pointer, std::int64_t Width) {
		auto Evil = const_cast<void *>(Pointer);
		this->Pointer = reinterpret_cast<T *>(Evil);
		this->Width = Width;
	}
	FramePointer(FramePointer &&) = default;
	FramePointer(const FramePointer &) = default;
	auto operator=(FramePointer &&)->FramePointer & = default;
	auto operator=(const FramePointer &)->FramePointer & = default;
	~FramePointer() = default;
	auto operator[](std::ptrdiff_t Row) {
		return Pointer + Row * Width;
	}
	auto operator[](std::ptrdiff_t Row) const {
		return const_cast<const T *>(Pointer + Row * Width);
	}
};
