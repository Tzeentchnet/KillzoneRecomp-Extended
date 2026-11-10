// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstddef>
#include <cstdint>

class GSTexture;

namespace kzgs
{
	void ProviderTestSetPath(const wchar_t* path);
	void ProviderTestAllowSpatialMock(bool allow);
	bool ProviderTestUpscale(GSTexture* source, int left, int top, int right, int bottom,
		int width, int height, uint64_t presentation, bool redraw, GSTexture*& output);
	void ProviderTestPresented(uint64_t presentation, bool succeeded, bool used);
	size_t ProviderTestRetiredCount();
	void ProviderTestCollect();
	void ProviderTestRedraw();
	bool ProviderTestReopenDevice();
}
