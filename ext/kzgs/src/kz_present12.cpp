// SPDX-License-Identifier: GPL-3.0+
// D3D12 swap-chain back-buffer capture before Present, including final scaling and CAS.

#include "kzgs_internal.h"

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "GS/Renderers/DX12/GSTexture12.h"

#define private public
#include "GS/Renderers/DX12/GSDevice12.h"
#undef private

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
	using EndPresentFn = void (*)(GSDevice12*);

	void** s_vtableCopy = nullptr;
	EndPresentFn s_origEndPresent = nullptr;
	kzgs::PresentSink s_sink = nullptr;
	int s_slot = -1;
	std::vector<uint8_t> s_rgba;

	bool Capture(GSDevice12* dev)
	{
		if (!dev || dev->m_swap_chain_buffers.empty())
			return false;

		GSTexture12* const texture = dev->m_swap_chain_buffers[dev->m_current_swap_chain_buffer].get();
		if (!texture || texture->GetFormat() != GSTexture::Format::Color)
			return false;

		const u32 w = static_cast<u32>(texture->GetWidth());
		const u32 h = static_cast<u32>(texture->GetHeight());
		std::unique_ptr<GSDownloadTexture> download =
			dev->CreateDownloadTexture(w, h, GSTexture::Format::Color);
		if (!download)
			return false;

		const GSVector4i rect(0, 0, static_cast<int>(w), static_cast<int>(h));
		download->CopyFromTexture(rect, texture, rect, 0u, true);
		download->Flush();
		if (!download->Map(rect))
			return false;

		s_rgba.resize(static_cast<size_t>(w) * h * 4u);
		for (u32 y = 0; y < h; ++y)
		{
			std::memcpy(s_rgba.data() + static_cast<size_t>(y) * w * 4u,
			            download->GetMapPointer() + static_cast<size_t>(y) * download->GetMapPitch(),
			            static_cast<size_t>(w) * 4u);
		}
		download->Unmap();
		s_sink(s_rgba.data(), static_cast<int>(w), static_cast<int>(h));
		return true;
	}

	void HookEndPresent(GSDevice12* self)
	{
		if (s_sink && !Capture(self))
		{
			static bool reported = false;
			if (!reported)
			{
				std::fprintf(stderr, "[kzgs] D3D12 back-buffer capture failed\n");
				reported = true;
			}
		}
		s_origEndPresent(self);
	}
}

namespace kzgs
{
	bool PresentCaptureD3D12(PresentSink sink)
	{
		auto* const dev = g_gs_device ? dynamic_cast<GSDevice12*>(g_gs_device.get()) : nullptr;
		if (!dev)
			return false;
		s_sink = sink;
		void** const live = *reinterpret_cast<void***>(dev);
		if (s_vtableCopy && live == s_vtableCopy + 1)
			return true;
		if (s_vtableCopy && live != s_vtableCopy + 1)
		{
			s_vtableCopy = nullptr;
			s_slot = -1;
		}
		if (!s_vtableCopy)
		{
			s_slot = EndPresentVtableIndex();
			if (s_slot < 0)
			{
				std::fprintf(stderr, "[kzgs] D3D12 EndPresent vtable slot not found\n");
				return false;
			}
			constexpr int kEntries = 192;
			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery(live - 1, &mbi, sizeof(mbi)) ||
				static_cast<size_t>(reinterpret_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize -
				                    reinterpret_cast<uint8_t*>(live - 1)) <
					(kEntries + 1) * sizeof(void*))
			{
				std::fprintf(stderr, "[kzgs] D3D12 vtable region is too small\n");
				return false;
			}
			s_vtableCopy = static_cast<void**>(std::calloc(kEntries + 1, sizeof(void*)));
			if (!s_vtableCopy)
			{
				std::fprintf(stderr, "[kzgs] D3D12 capture vtable allocation failed\n");
				return false;
			}
			std::memcpy(s_vtableCopy, live - 1, (kEntries + 1) * sizeof(void*));
			s_origEndPresent = reinterpret_cast<EndPresentFn>(live[s_slot]);
			s_vtableCopy[1 + s_slot] = reinterpret_cast<void*>(&HookEndPresent);
		}
		*reinterpret_cast<void***>(dev) = s_vtableCopy + 1;
		std::fprintf(stderr, "[kzgs] D3D12 swap-chain capture installed (EndPresent slot %d)\n", s_slot);
		return true;
	}

	bool InjectD3D12DeviceLoss()
	{
		auto* const dev = g_gs_device ? dynamic_cast<GSDevice12*>(g_gs_device.get()) : nullptr;
		if (!dev)
			return false;
		dev->m_device_lost = true;
		return true;
	}
} // namespace kzgs
