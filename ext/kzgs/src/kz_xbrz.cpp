// SPDX-License-Identifier: GPL-3.0+

#include "kz_xbrz.h"

#include "xbrz.h"

#include <limits>

KzXbrzScaleResult kzXbrzGetOutputSize(
	std::uint32_t width, std::uint32_t height, std::uint32_t factor, KzXbrzImageSize* output) noexcept
{
	if (!output)
		return KzXbrzScaleResult::NullArgument;

	*output = {};
	if (width == 0 || height == 0)
		return KzXbrzScaleResult::EmptyImage;
	if (factor != 2)
		return KzXbrzScaleResult::UnsupportedFactor;

	constexpr std::uint32_t max_dimension = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
	if (width > max_dimension / factor || height > max_dimension / factor)
		return KzXbrzScaleResult::DimensionOverflow;

	const std::uint32_t scaled_width = width * factor;
	const std::uint32_t scaled_height = height * factor;
	const std::size_t scaled_width_size = static_cast<std::size_t>(scaled_width);
	if (scaled_width_size > std::numeric_limits<std::size_t>::max() / scaled_height)
		return KzXbrzScaleResult::DimensionOverflow;

	const std::size_t pixels = scaled_width_size * scaled_height;
	if (pixels > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
		return KzXbrzScaleResult::DimensionOverflow;

	output->width = scaled_width;
	output->height = scaled_height;
	output->pixels = pixels;
	output->bytes = pixels * sizeof(std::uint32_t);
	return KzXbrzScaleResult::Success;
}

KzXbrzScaleResult kzXbrzScaleRgba(
	const std::uint32_t* source, std::size_t source_pixels, std::uint32_t width, std::uint32_t height,
	std::uint32_t factor, std::uint32_t* destination, std::size_t destination_pixels) noexcept
{
	if (!source || !destination)
		return KzXbrzScaleResult::NullArgument;

	KzXbrzImageSize output;
	const KzXbrzScaleResult size_result = kzXbrzGetOutputSize(width, height, factor, &output);
	if (size_result != KzXbrzScaleResult::Success)
		return size_result;

	const std::size_t source_width = static_cast<std::size_t>(width);
	if (source_width > std::numeric_limits<std::size_t>::max() / height)
		return KzXbrzScaleResult::DimensionOverflow;

	if (source_pixels < source_width * height)
		return KzXbrzScaleResult::SourceTooSmall;
	if (destination_pixels < output.pixels)
		return KzXbrzScaleResult::DestinationTooSmall;

	xbrz::scale(
		factor, source, destination, static_cast<int>(width), static_cast<int>(height), xbrz::ColorFormat::ARGB);
	return KzXbrzScaleResult::Success;
}

const char* kzXbrzScaleResultName(KzXbrzScaleResult result) noexcept
{
	switch (result)
	{
		case KzXbrzScaleResult::Success:
			return "success";
		case KzXbrzScaleResult::NullArgument:
			return "null_argument";
		case KzXbrzScaleResult::EmptyImage:
			return "empty_image";
		case KzXbrzScaleResult::UnsupportedFactor:
			return "unsupported_factor";
		case KzXbrzScaleResult::DimensionOverflow:
			return "dimension_overflow";
		case KzXbrzScaleResult::SourceTooSmall:
			return "source_too_small";
		case KzXbrzScaleResult::DestinationTooSmall:
			return "destination_too_small";
	}
	return "unknown";
}
