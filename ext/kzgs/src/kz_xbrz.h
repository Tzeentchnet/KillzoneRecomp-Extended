// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <cstddef>
#include <cstdint>

enum class KzXbrzScaleResult : std::uint8_t
{
	Success,
	NullArgument,
	EmptyImage,
	UnsupportedFactor,
	DimensionOverflow,
	SourceTooSmall,
	DestinationTooSmall,
};

struct KzXbrzImageSize
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::size_t pixels = 0;
	std::size_t bytes = 0;
};

KzXbrzScaleResult kzXbrzGetOutputSize(
	std::uint32_t width, std::uint32_t height, std::uint32_t factor, KzXbrzImageSize* output) noexcept;

KzXbrzScaleResult kzXbrzScaleRgba(
	const std::uint32_t* source, std::size_t source_pixels, std::uint32_t width, std::uint32_t height,
	std::uint32_t factor, std::uint32_t* destination, std::size_t destination_pixels) noexcept;

const char* kzXbrzScaleResultName(KzXbrzScaleResult result) noexcept;
