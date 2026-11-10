// kzgs_test: opens kzgs on a (hidden) Win32 window, programs a 640x448 NTSC display, draws a cleared framebuffer
// with three sprites and a triangle over GIF PATH3, presents, reads the frame back and checks pixel colors.
// Then switches to 2x upscaling and checks the readback doubles in size with the same picture, stresses the ring
// buffer with >32 MB of chunked PATH2 IMAGE uploads, and switches renderer on the same window (scene over PATH1).
//
// Usage: kzgs_test [--renderer d3d11|d3d12|vulkan|software] [--warp] [--golden] [--out <dir>] [--show] [--vsync]
//                  [--raw-gif] [--frames N] [--stress-uploads N]
// Exit code 0 = pass. Writes test/out.png (1x), out_2x.png, out_stress.png and out_switch.png.
// If the hardware D3D device cannot be created (e.g. no GPU access), it retries on WARP and says so.
// SPDX-License-Identifier: GPL-3.0+

#include "kzgs.h"
#include "kzgs_fsr1_runtime.h"
#include "kzgs_internal.h"
#include "kzgs_mock_control.h"
#include "kzgs_provider_test_hooks.h"
#include "kzgs_upscaler_provider.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef KZGS_TEST_OUT_DIR
#define KZGS_TEST_OUT_DIR "."
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Minimal PNG writer (zlib stored blocks, no compression) so the test has no image-library dependency.
// ---------------------------------------------------------------------------------------------------------------------
static uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0)
{
	crc = ~crc;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= p[i];
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static void PutBE32(std::vector<uint8_t>& v, uint32_t x)
{
	v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}

static void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
	PutBE32(out, uint32_t(data.size()));
	std::vector<uint8_t> td(type, type + 4);
	td.insert(td.end(), data.begin(), data.end());
	out.insert(out.end(), td.begin(), td.end());
	PutBE32(out, Crc32(td.data(), td.size()));
}

static bool WritePNG(const char* path, const std::vector<uint8_t>& rgba, int w, int h)
{
	std::vector<uint8_t> raw;
	raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
	for (int y = 0; y < h; y++)
	{
		raw.push_back(0);
		const uint8_t* row = rgba.data() + size_t(y) * w * 4;
		for (int x = 0; x < w; x++)
		{
			raw.push_back(row[x * 4 + 0]); raw.push_back(row[x * 4 + 1]); raw.push_back(row[x * 4 + 2]);
			raw.push_back(255); // PS2 alpha is 0..128 and meaningless for display
		}
	}
	std::vector<uint8_t> z = {0x78, 0x01};
	uint32_t a = 1, b = 0;
	for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
	for (size_t off = 0; off < raw.size(); off += 65535)
	{
		const size_t n = std::min<size_t>(65535, raw.size() - off);
		z.push_back(off + n == raw.size() ? 1 : 0);
		z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
		z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
		z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
	}
	PutBE32(z, (b << 16) | a);

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	std::vector<uint8_t> ihdr;
	PutBE32(ihdr, uint32_t(w)); PutBE32(ihdr, uint32_t(h));
	ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
	Chunk(png, "IHDR", ihdr);
	Chunk(png, "IDAT", z);
	Chunk(png, "IEND", {});
	FILE* f = std::fopen(path, "wb");
	if (!f)
		return false;
	std::fwrite(png.data(), 1, png.size(), f);
	std::fclose(f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// GS register helpers
// ---------------------------------------------------------------------------------------------------------------------
alignas(16) static uint8_t g_regs[0x2000];

static void SetPriv(uint32_t offset, uint64_t value)
{
	std::memcpy(g_regs + offset, &value, sizeof(value));
}

static void SetupNTSC640x448()
{
	std::memset(g_regs, 0, sizeof(g_regs));
	// PMODE: EN1=1, CRTMD=1, MMOD=1 (use ALP), ALP=0xFF -> read circuit 1 only, opaque.
	SetPriv(0x0000, 1ull | (1ull << 2) | (1ull << 5) | (0xFFull << 8));
	// SMODE1: RC=4, LC=32, CMOD=2 (NTSC colour subcarrier) -> PCSX2 GetVideoMode() = NTSC.
	SetPriv(0x0010, 4ull | (32ull << 3) | (2ull << 13));
	// SMODE2: INT=1, FFMD=0 (interlaced; each field reads alternate lines of the full 448-line buffer).
	// (FFMD=1 would read a half-height 224-line field buffer and line-double it.)
	SetPriv(0x0020, 1ull);
	// DISPFB1: FBP=0, FBW=10 (640 px), PSM=CT32, DBX=DBY=0.
	SetPriv(0x0070, 10ull << 9);
	// DISPLAY1: DX=636, DY=50, MAGH=3 (x4), MAGV=0, DW=2559 (640*4-1), DH=447.
	SetPriv(0x0080, 636ull | (50ull << 12) | (3ull << 23) | (2559ull << 32) | (447ull << 44));
	// BGCOLOR black. CSR: FIELD=0.
}

struct GifBuilder
{
	std::vector<uint64_t> q; // pairs of u64 = one quadword

	void AD(uint8_t reg, uint64_t data)
	{
		q.push_back(data);
		q.push_back(reg);
	}

	// Emit as one PACKED A+D packet (GIFtag NREG=1, REGS=0xE), EOP set.
	std::vector<uint8_t> Build() const
	{
		const uint64_t nloop = q.size() / 2;
		const uint64_t tag_lo = nloop | (1ull << 15) /*EOP*/ | (0ull << 58) /*FLG=PACKED*/ | (1ull << 60) /*NREG=1*/;
		const uint64_t tag_hi = 0xE; // A+D
		std::vector<uint8_t> out(16 + q.size() * 8);
		std::memcpy(out.data(), &tag_lo, 8);
		std::memcpy(out.data() + 8, &tag_hi, 8);
		std::memcpy(out.data() + 16, q.data(), q.size() * 8);
		return out;
	}
};

static uint64_t RGBAQ(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0x80)
{
	return uint64_t(r) | (uint64_t(g) << 8) | (uint64_t(b) << 16) | (uint64_t(a) << 24) | (0x3F800000ull << 32);
}

static uint64_t XYZ(int x, int y, uint32_t z = 0)
{
	return uint64_t(uint16_t(x << 4)) | (uint64_t(uint16_t(y << 4)) << 16) | (uint64_t(z) << 32);
}

enum : uint8_t
{
	REG_PRIM = 0x00, REG_RGBAQ = 0x01, REG_XYZ2 = 0x05, REG_XYOFFSET_1 = 0x18, REG_PRMODECONT = 0x1A,
	REG_SCISSOR_1 = 0x40, REG_DTHE = 0x45, REG_COLCLAMP = 0x46, REG_TEST_1 = 0x47, REG_PABE = 0x49,
	REG_FBA_1 = 0x4A, REG_FRAME_1 = 0x4C, REG_ZBUF_1 = 0x4E,
};

enum : uint64_t { PRIM_TRIANGLE = 3, PRIM_SPRITE = 6 };

struct Box { int x0, y0, x1, y1; uint8_t r, g, b; const char* name; };
static const Box kBackground = {0, 0, 640, 448, 0, 0, 128, "background"};
static const Box kRed = {64, 64, 256, 192, 255, 0, 0, "red sprite"};
static const Box kGreen = {320, 240, 576, 400, 0, 255, 0, "green sprite"};
static const Box kWhite = {32, 320, 160, 416, 255, 255, 255, "white sprite"};
// Yellow triangle (400,40)-(600,40)-(500,200); probe at its centroid.
static const int kTriProbeX = 500, kTriProbeY = 93;

static std::vector<uint8_t> BuildScene()
{
	GifBuilder g;
	g.AD(REG_FRAME_1, (0ull) | (10ull << 16) | (0ull << 24)); // FBP=0, FBW=10, PSM=CT32
	g.AD(REG_ZBUF_1, (140ull) | (0ull << 24) | (1ull << 32));  // ZBP past the framebuffer, ZMSK=1 (no Z writes)
	g.AD(REG_XYOFFSET_1, 0);
	g.AD(REG_SCISSOR_1, 0ull | (639ull << 16) | (0ull << 32) | (447ull << 48));
	g.AD(REG_TEST_1, (1ull << 16) | (1ull << 17));              // ZTE=1, ZTST=ALWAYS
	g.AD(REG_PRMODECONT, 1);
	g.AD(REG_DTHE, 0);
	g.AD(REG_COLCLAMP, 1);
	g.AD(REG_PABE, 0);
	g.AD(REG_FBA_1, 0);

	for (const Box* b : {&kBackground, &kRed, &kGreen, &kWhite})
	{
		g.AD(REG_PRIM, PRIM_SPRITE);
		g.AD(REG_RGBAQ, RGBAQ(b->r, b->g, b->b));
		g.AD(REG_XYZ2, XYZ(b->x0, b->y0));
		g.AD(REG_XYZ2, XYZ(b->x1, b->y1));
	}

	g.AD(REG_PRIM, PRIM_TRIANGLE);
	g.AD(REG_RGBAQ, RGBAQ(255, 255, 0));
	g.AD(REG_XYZ2, XYZ(400, 40));
	g.AD(REG_XYZ2, XYZ(600, 40));
	g.AD(REG_XYZ2, XYZ(500, 200));
	return g.Build();
}

// ---------------------------------------------------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND CreateTestWindow(bool show)
{
	WNDCLASSEXW wc = {sizeof(wc)};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"kzgs_test";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassExW(&wc);
	RECT rc = {0, 0, 640, 448};
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, L"kzgs_test", L"kzgs_test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
	if (hwnd && show)
		ShowWindow(hwnd, SW_SHOW);
	return hwnd;
}

static void PumpMessages();

static void ResizeTestWindow(HWND hwnd, int width, int height)
{
	RECT rc = {0, 0, width, height};
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
	             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	PumpMessages();
}

static void ResizeTestWindowUnconstrained(HWND hwnd, int width, int height)
{
	SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP);
	SetWindowPos(hwnd, nullptr, 0, 0, width, height,
	             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	PumpMessages();
}

static void RestoreTestWindowStyle(HWND hwnd)
{
	SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
	             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	PumpMessages();
}

static void PumpMessages()
{
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

static std::atomic<int> g_vulkan_validation_errors{0};
static std::atomic<bool> g_vulkan_validation_unavailable{false};
static std::atomic<bool> g_vulkan_validation_seen{false};

static void Log(int level, const char* msg)
{
	static const char* tags[] = {"E", "W", "I"};
	if (std::strstr(msg, "SYNC-HAZARD") || std::strstr(msg, "Validation Error") ||
		(std::strstr(msg, "VUID-") && !std::strstr(msg, "VUID-VkDeviceCreateInfo-enabledLayerCount-12384")))
		g_vulkan_validation_errors.fetch_add(1, std::memory_order_relaxed);
	if (std::strstr(msg, "validation/debug layers requested but are unavailable"))
		g_vulkan_validation_unavailable.store(true, std::memory_order_relaxed);
	if (std::strstr(msg, "Inserted device layer \"VK_LAYER_KHRONOS_validation\""))
		g_vulkan_validation_seen.store(true, std::memory_order_relaxed);
	std::printf("[gs %s] %s\n", tags[level < 0 || level > 2 ? 2 : level], msg);
}

static int g_failures = 0;
static bool g_check_golden = false;
static std::string g_output_dir = KZGS_TEST_OUT_DIR;

static void CheckPixel(const std::vector<uint8_t>& rgba, int w, int h, int scale, int px, int py, uint8_t r,
	uint8_t g, uint8_t b, const char* what)
{
	const int x = px * scale + scale / 2, y = py * scale + scale / 2;
	if (x >= w || y >= h)
	{
		std::printf("  FAIL %-12s (%d,%d) outside %dx%d frame\n", what, x, y, w, h);
		g_failures++;
		return;
	}
	const uint8_t* p = rgba.data() + (size_t(y) * w + x) * 4;
	const bool ok = std::abs(p[0] - r) <= 8 && std::abs(p[1] - g) <= 8 && std::abs(p[2] - b) <= 8;
	std::printf("  %s %-12s at (%4d,%4d): got (%3u,%3u,%3u) want (%3u,%3u,%3u)\n", ok ? "ok  " : "FAIL", what, x, y,
		p[0], p[1], p[2], r, g, b);
	if (!ok)
		g_failures++;
}

static double EdgeDistance(double ax, double ay, double bx, double by, double px, double py)
{
	const double edge = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
	return edge / std::hypot(bx - ax, by - ay);
}

static void CheckGoldenFrame(const std::vector<uint8_t>& rgba, int w, int h, int scale)
{
	size_t checked = 0;
	size_t mismatches = 0;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			const double native_offset = scale - 1.0;
			const double px = (x + 0.5 - native_offset) / scale;
			const double py = (y + 0.5 - native_offset) / scale;
			uint8_t er = kBackground.r, eg = kBackground.g, eb = kBackground.b;
			for (const Box* box : {&kRed, &kGreen, &kWhite})
			{
				if (px >= box->x0 && px < box->x1 && py >= box->y0 && py < box->y1)
				{
					er = box->r;
					eg = box->g;
					eb = box->b;
				}
			}

			const double e0 = EdgeDistance(400, 40, 600, 40, px, py);
			const double e1 = EdgeDistance(600, 40, 500, 200, px, py);
			const double e2 = EdgeDistance(500, 200, 400, 40, px, py);
			if (std::min({std::abs(e0), std::abs(e1), std::abs(e2)}) < 1.5)
				continue;
			if (e0 > 0 && e1 > 0 && e2 > 0)
			{
				er = 255;
				eg = 255;
				eb = 0;
			}

			const uint8_t* actual = rgba.data() + (size_t(y) * w + x) * 4;
			checked++;
			if (std::abs(int(actual[0]) - er) <= 8 &&
				std::abs(int(actual[1]) - eg) <= 8 &&
				std::abs(int(actual[2]) - eb) <= 8)
				continue;
			if (mismatches < 8)
			{
				std::printf("  golden mismatch at (%d,%d): got (%u,%u,%u), want (%u,%u,%u)\n",
					x, y, actual[0], actual[1], actual[2], er, eg, eb);
			}
			mismatches++;
		}
	}

	std::printf("  golden oracle: %zu pixels checked, %zu mismatches\n", checked, mismatches);
	if (mismatches != 0)
		g_failures++;
}

// Host->local IMAGE transfer (BITBLTBUF/TRXPOS/TRXREG/TRXDIR via A+D, then an IMAGE-mode GIFtag) of a solid
// CT32 rectangle.
static std::vector<uint8_t> BuildUpload(uint32_t dbp, uint32_t dbw, int x, int y, int w, int h, uint32_t abgr)
{
	GifBuilder g;
	g.AD(0x50, (uint64_t(dbp) << 32) | (uint64_t(dbw) << 48));             // BITBLTBUF, DPSM=CT32
	g.AD(0x51, (uint64_t(x) << 32) | (uint64_t(y) << 48));                 // TRXPOS
	g.AD(0x52, uint64_t(w) | (uint64_t(h) << 32));                         // TRXREG
	g.AD(0x53, 0);                                                         // TRXDIR host->local
	std::vector<uint8_t> out = g.Build();
	// Build() set EOP on the A+D tag; clear it, the IMAGE tag below ends the packet.
	out[1] &= 0x7F;
	const uint32_t qw = uint32_t(w) * uint32_t(h) * 4 / 16;
	const uint64_t tag_lo = uint64_t(qw) | (1ull << 15) | (2ull << 58); // NLOOP=qw, EOP, FLG=IMAGE
	const uint64_t tag_hi = 0;
	const size_t base = out.size();
	out.resize(base + 16 + size_t(qw) * 16);
	std::memcpy(out.data() + base, &tag_lo, 8);
	std::memcpy(out.data() + base + 8, &tag_hi, 8);
	uint32_t* px = reinterpret_cast<uint32_t*>(out.data() + base + 16);
	for (size_t i = 0; i < size_t(qw) * 4; i++)
		px[i] = abgr;
	return out;
}

static void SendChunked(int path, const std::vector<uint8_t>& pkt, uint32_t chunk_qw)
{
	const uint32_t total = uint32_t(pkt.size() / 16);
	for (uint32_t off = 0; off < total; off += chunk_qw)
		kzgsGifTransfer(path, pkt.data() + size_t(off) * 16, std::min(chunk_qw, total - off));
}

static std::vector<uint8_t> g_last_rgba;
static int g_last_w = 0, g_last_h = 0;

static void PresentFrames(const std::vector<uint8_t>& packet, int frames, int gif_path)
{
	for (int f = 0; f < frames; f++)
	{
		kzgsGifTransfer(gif_path, packet.data(), uint32_t(packet.size() / 16));
		// Toggle the FIELD bit in CSR like the real GS does each vsync.
		SetPriv(0x1000, (f & 1) ? (1ull << 13) : 0);
		kzgsVsync(f & 1);
		PumpMessages();
	}
}

static bool RenderAndCheck(const std::vector<uint8_t>& packet, int frames, int scale, const char* png_name, int gif_path = 3)
{
	PresentFrames(packet, frames, gif_path);

	std::vector<uint8_t>& rgba = g_last_rgba;
	int& w = g_last_w;
	int& h = g_last_h;
	const DWORD rb0 = GetTickCount();
	const bool rb_ok = kzgsReadback(rgba, w, h);
	std::printf("  frames+readback took %lu ms\n", GetTickCount() - rb0);
	if (!rb_ok)
	{
		std::printf("  FAIL readback returned no frame\n");
		g_failures++;
		return false;
	}
	std::printf("  readback %dx%d (expected %dx%d)\n", w, h, 640 * scale, 448 * scale);
	if (w != 640 * scale || h != 448 * scale)
	{
		std::printf("  FAIL readback size\n");
		g_failures++;
	}

	const std::string path = (std::filesystem::path(g_output_dir) / png_name).string();
	const bool wrote = WritePNG(path.c_str(), rgba, w, h);
	std::printf("  wrote %s: %s\n", path.c_str(), wrote ? "yes" : "NO");
	if (!wrote)
		g_failures++;

	CheckPixel(rgba, w, h, scale, 16, 16, kBackground.r, kBackground.g, kBackground.b, "background");
	CheckPixel(rgba, w, h, scale, 160, 128, kRed.r, kRed.g, kRed.b, "red");
	CheckPixel(rgba, w, h, scale, 448, 320, kGreen.r, kGreen.g, kGreen.b, "green");
	CheckPixel(rgba, w, h, scale, 96, 368, kWhite.r, kWhite.g, kWhite.b, "white");
	CheckPixel(rgba, w, h, scale, kTriProbeX, kTriProbeY, 255, 255, 0, "yellow tri");
	CheckPixel(rgba, w, h, scale, 300, 100, kBackground.r, kBackground.g, kBackground.b, "gap");
	// Sprite edges: last covered pixel is x1-1 (GS sprites exclude the right/bottom edge).
	CheckPixel(rgba, w, h, scale, 255, 191, kRed.r, kRed.g, kRed.b, "red edge");
	CheckPixel(rgba, w, h, scale, 256, 192, kBackground.r, kBackground.g, kBackground.b, "past edge");
	if (g_check_golden)
		CheckGoldenFrame(rgba, w, h, scale);
	return true;
}

static bool RenderAndCheckBackbuffer(
	const std::vector<uint8_t>& packet, int frames, int expected_w, int expected_h, const char* png_name)
{
	kzgsBurstArm(1, -1, -1);
	PresentFrames(packet, frames, 3);
	kzgsSync();
	std::vector<KzgsBurstFrame> captured;
	if (!kzgsBurstTake(captured) || captured.size() != 1u)
	{
		std::printf("  FAIL swap-chain capture returned %zu frames\n", captured.size());
		g_failures++;
		return false;
	}
	const KzgsBurstFrame& frame = captured[0];
	std::printf("  swap-chain readback %dx%d (expected %dx%d)\n", frame.width, frame.height, expected_w, expected_h);
	if (frame.width != expected_w || frame.height != expected_h)
	{
		std::printf("  FAIL swap-chain readback size\n");
		g_failures++;
	}
	size_t non_black = 0;
	for (size_t i = 0; i + 3 < frame.rgba.size(); i += 4)
		non_black += (frame.rgba[i] + frame.rgba[i + 1] + frame.rgba[i + 2]) > 30;
	if (frame.rgba.empty() || non_black * 100u < (frame.rgba.size() / 4u))
	{
		std::printf("  FAIL swap-chain readback is empty/black\n");
		g_failures++;
	}
	const std::string path = (std::filesystem::path(g_output_dir) / png_name).string();
	if (!WritePNG(path.c_str(), frame.rgba, frame.width, frame.height))
	{
		std::printf("  FAIL writing %s\n", path.c_str());
		g_failures++;
	}
	return true;
}

static void CheckResolutionInfo(int render_w, int render_h, int output_w, int output_h)
{
	KzgsResolutionInfo info;
	const bool available = kzgsGetResolutionInfo(info);
	std::printf("  resolution contract: render %dx%d -> output %dx%d\n",
		info.renderWidth, info.renderHeight, info.outputWidth, info.outputHeight);
	if (!available || info.renderWidth != render_w || info.renderHeight != render_h ||
		info.outputWidth != output_w || info.outputHeight != output_h)
	{
		std::printf("  FAIL resolution contract (expected render %dx%d -> output %dx%d)\n",
			render_w, render_h, output_w, output_h);
		g_failures++;
	}
}

static void CheckUpscalerStatus(KzgsUpscaler requested, int sharpening, KzgsUpscalerReason reason,
	KzgsUpscaler effective = KzgsUpscaler::Off)
{
	KzgsUpscalerStatus status;
	if (!kzgsGetUpscalerStatus(status) || status.requested != requested ||
		status.effective != effective || status.requestedSharpening != sharpening ||
		status.effectiveSharpening != (effective == KzgsUpscaler::Off ? 0 : sharpening) || status.reason != reason)
	{
		std::printf("  FAIL presentation upscaler status: requested=%s effective=%s reason=%s (expected %s/%s)\n",
			kzgsUpscalerName(requested), kzgsUpscalerName(status.effective), kzgsUpscalerReasonName(status.reason),
			kzgsUpscalerName(effective), kzgsUpscalerReasonName(reason));
		g_failures++;
	}
}

static void CheckUpscalerClosed()
{
	KzgsUpscalerStatus status;
	if (kzgsGetUpscalerStatus(status))
	{
		std::printf("  FAIL upscaler status available while closed\n");
		g_failures++;
	}
}

static bool ReadPresented(std::vector<uint8_t>& pixels, int expectedWidth, int expectedHeight)
{
	int width = 0, height = 0;
	if (!kzgsReadback(pixels, width, height, expectedWidth, expectedHeight) ||
		width != expectedWidth || height != expectedHeight)
	{
		std::printf("  FAIL presented readback: got %dx%d, expected %dx%d\n",
			width, height, expectedWidth, expectedHeight);
		++g_failures;
		return false;
	}
	return true;
}

static void CheckFsr1Vulkan(HWND hwnd, KzgsConfig& cfg, const std::vector<uint8_t>& scene, int frames)
{
	std::printf("== Vulkan FSR1 activation, capture, resize and recreation\n");
	const kz::host::NativeWindow window = kz::host::NativeWindow::win32(hwnd);
	ResizeTestWindowUnconstrained(hwnd, 1003, 757);
	kzgsResize(1003, 757);
	cfg.upscaler = KzgsUpscaler::Off;
	cfg.upscalerSharpening = 0;
	cfg.bilinearPresent = true;
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	std::vector<uint8_t> baseline;
	ReadPresented(baseline, 1003, 757);

	cfg.upscaler = KzgsUpscaler::FSR1;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 0, KzgsUpscalerReason::WaitingForFrame);
	const uint64_t beforeFailure = kzgs::Fsr1TestDispatchCount();
	kzgs::Fsr1TestFailNextInitialization();
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 0, KzgsUpscalerReason::InitializationFailed);
	std::vector<uint8_t> failureFallback;
	if (ReadPresented(failureFallback, 1003, 757) &&
		(failureFallback != baseline || kzgs::Fsr1TestDispatchCount() != beforeFailure))
	{
		std::printf("  FAIL initialization failure did not preserve the ordinary presentation\n");
		++g_failures;
	}
	cfg.upscaler = KzgsUpscaler::Off;
	kzgsUpdateConfig(cfg);
	cfg.upscaler = KzgsUpscaler::FSR1;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 0, KzgsUpscalerReason::WaitingForFrame);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 0, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);
	std::vector<uint8_t> easu, repeated;
	const uint64_t beforeReadback = kzgs::Fsr1TestDispatchCount();
	if (ReadPresented(easu, 1003, 757) && ReadPresented(repeated, 1003, 757))
	{
		if (easu == baseline)
		{
			std::printf("  FAIL EASU output is identical to ordinary presentation\n");
			++g_failures;
		}
		if (easu != repeated)
		{
			std::printf("  FAIL repeated presented readback changed without another present\n");
			++g_failures;
		}
	}
	if (kzgs::Fsr1TestDispatchCount() != beforeReadback)
	{
		std::printf("  FAIL presented readback re-dispatched FSR1\n");
		++g_failures;
	}

	cfg.upscalerSharpening = 100;
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);
	std::vector<uint8_t> rcas;
	if (ReadPresented(rcas, 1003, 757) && !easu.empty() && rcas == easu)
	{
		std::printf("  FAIL RCAS did not affect the upscaled frame\n");
		++g_failures;
	}

	cfg.casSharpness = 30;
	kzgsUpdateConfig(cfg);
	kzgsBurstArm(1, -1, -1);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);
	std::vector<KzgsBurstFrame> captured;
	std::vector<uint8_t> withCas;
	if (!kzgsBurstTake(captured) || captured.size() != 1 || captured[0].width != 1003 || captured[0].height != 757 ||
		!ReadPresented(withCas, 1003, 757) || captured[0].rgba != withCas)
	{
		std::printf("  FAIL completed upscaled presented capture/replay\n");
		++g_failures;
	}
	if (!rcas.empty() && withCas == rcas)
	{
		std::printf("  FAIL completed capture did not include CAS\n");
		++g_failures;
	}

	const uint64_t beforeRetarget = kzgs::Fsr1TestDispatchCount();
	std::vector<uint8_t> fixedAspect;
	if (ReadPresented(fixedAspect, 701, 701))
	{
		const size_t sample = (35u * 701u + 350u) * 4u;
		if (fixedAspect[sample] > 4 || fixedAspect[sample + 1] > 4 || fixedAspect[sample + 2] > 4 ||
			kzgs::Fsr1TestDispatchCount() != beforeRetarget)
		{
			std::printf("  FAIL retargeted fixed-aspect capture did not retain letterboxing\n");
			++g_failures;
		}
	}
	cfg.casSharpness = 0;
	cfg.aspect = KzgsAspect::Stretch;
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	std::vector<uint8_t> stretched;
	if (ReadPresented(stretched, 701, 701))
	{
		const size_t sample = (35u * 701u + 350u) * 4u;
		if (stretched[sample + 2] < 80)
		{
			std::printf("  FAIL retargeted stretch capture introduced letterboxing\n");
			++g_failures;
		}
	}
	cfg.aspect = KzgsAspect::Ratio4_3;
	cfg.crop[0] = 7; cfg.crop[1] = 5; cfg.crop[2] = 11; cfg.crop[3] = 3;
	ResizeTestWindowUnconstrained(hwnd, 1001, 751);
	kzgsResize(1001, 751);
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);
	std::vector<uint8_t> cropped;
	ReadPresented(cropped, 1001, 751);

	ResizeTestWindowUnconstrained(hwnd, 320, 240);
	kzgsResize(320, 240);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::NoUpscaleNeeded);
	std::vector<uint8_t> noUpscale, off;
	ReadPresented(noUpscale, 320, 240);
	cfg.upscaler = KzgsUpscaler::Off;
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	if (ReadPresented(off, 320, 240) && noUpscale != off)
	{
		std::printf("  FAIL no-upscale fallback changed presentation\n");
		++g_failures;
	}

	ResizeTestWindowUnconstrained(hwnd, 1003, 757);
	kzgsResize(1003, 757);
	cfg.upscaler = KzgsUpscaler::FSR1;
	cfg.renderer = KzgsRenderer::D3D11;
	cfg.debugDevice = false;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::NotImplemented);
	cfg.renderer = KzgsRenderer::Vulkan;
	cfg.debugDevice = true;
	kzgsUpdateConfig(cfg);
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);

	kzgsClose();
	CheckUpscalerClosed();
	std::string error;
	cfg.debugDevice = false;
	if (!kzgsOpen({}, cfg, g_regs, &error))
	{
		std::printf("  FAIL surfaceless Vulkan reopen: %s\n", error.c_str());
		++g_failures;
	}
	else
	{
		CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::NoSurface);
		kzgsClose();
	}
	cfg.debugDevice = true;
	if (!kzgsOpen(window, cfg, g_regs, &error))
	{
		std::printf("  FAIL Vulkan device recreation: %s\n", error.c_str());
		++g_failures;
		return;
	}
	PresentFrames(scene, frames, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::FSR1, 100, KzgsUpscalerReason::Active, KzgsUpscaler::FSR1);

	if (!g_vulkan_validation_seen.load(std::memory_order_relaxed) ||
		g_vulkan_validation_unavailable.load(std::memory_order_relaxed) ||
		g_vulkan_validation_errors.load(std::memory_order_relaxed) != 0)
	{
		std::printf("  FAIL Vulkan validation: seen=%d unavailable=%d errors=%d\n",
			g_vulkan_validation_seen.load(), g_vulkan_validation_unavailable.load(), g_vulkan_validation_errors.load());
		++g_failures;
	}
	cfg.upscaler = KzgsUpscaler::Off;
	cfg.upscalerSharpening = 0;
	cfg.bilinearPresent = false;
	std::fill(std::begin(cfg.crop), std::end(cfg.crop), 0);
	ResizeTestWindow(hwnd, 640, 448);
	RestoreTestWindowStyle(hwnd);
	kzgsResize(640, 448);
	kzgsUpdateConfig(cfg);
	kzgsSync();
}

static void CheckProviderVulkan(HWND hwnd, const std::string& path, const std::vector<uint8_t>& scene)
{
	std::printf("== Vulkan provider renderer integration\n");
	const std::wstring dllPath = std::filesystem::absolute(path).wstring();
	HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	const auto setMode = module ? reinterpret_cast<KzgsMockSetMode>(GetProcAddress(module, "kzgs_mock_set_mode")) : nullptr;
	const auto getStats = module ? reinterpret_cast<KzgsMockGetStats>(GetProcAddress(module, "kzgs_mock_get_stats")) : nullptr;
	if (!module || !setMode || !getStats)
	{
		std::printf("  FAIL mock provider could not be loaded\n");
		++g_failures;
		if (module)
			FreeLibrary(module);
		return;
	}
	const auto stats = [&]() {
		KzgsMockStats value{};
		getStats(&value);
		return value;
	};
	const auto check = [](bool condition, const char* message) {
		std::printf("  %s %s\n", condition ? "ok  " : "FAIL", message);
		if (!condition)
			++g_failures;
	};

	setMode(0);
	kzgs::ProviderTestSetPath(dllPath.c_str());
	ResizeTestWindowUnconstrained(hwnd, 1003, 757);
	KzgsConfig cfg;
	cfg.renderer = KzgsRenderer::Vulkan;
	cfg.upscaler = KzgsUpscaler::DLSS;
	cfg.debugDevice = true;
	cfg.vsync = false;
	cfg.bilinearPresent = true;
	std::string error;
	if (!kzgsOpen(kz::host::NativeWindow::win32(hwnd), cfg, g_regs, &error))
	{
		std::printf("  FAIL Vulkan provider open: %s\n", error.c_str());
		++g_failures;
		kzgs::ProviderTestSetPath(nullptr);
		FreeLibrary(module);
		return;
	}
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::WaitingForFrame);
	PresentFrames(scene, 2, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);
	KzgsMockStats previous = stats();
	check(previous.creates > 0 && previous.evaluates > 0, "renderer dispatched provider and presented");
	std::vector<uint8_t> providerCapture, repeatedCapture;
	if (ReadPresented(providerCapture, 1003, 757) && ReadPresented(repeatedCapture, 1003, 757))
		check(providerCapture == repeatedCapture && stats().evaluates == previous.evaluates,
			"presented readback retains provider output without redispatch");
	kzgsBurstArm(1, -1, -1);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	std::vector<KzgsBurstFrame> captured;
	std::vector<uint8_t> completedCapture;
	if (kzgsBurstTake(captured) && captured.size() == 1 && ReadPresented(completedCapture, 1003, 757))
		check(captured[0].rgba == completedCapture, "provider burst captures completed presented output");
	else
		check(false, "provider burst captures completed presented output");
	previous = stats();

	kzgsVsync(0, false);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::DuplicatePresent);
	check(stats().evaluates == previous.evaluates, "idle VSync does not dispatch a possible duplicate");
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);
	check(stats().last_reset_reason == KZGS_PROVIDER_RESET_DISCONTINUITY, "idle VSync resets temporal history");

	previous = stats();
	kzgs::RunOnGSThread([] { kzgs::ProviderTestRedraw(); });
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::DuplicatePresent);
	check(stats().evaluates == previous.evaluates, "renderer redraw does not dispatch twice");
	std::vector<uint8_t> redrawCapture;
	ReadPresented(redrawCapture, 1003, 757);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);

	cfg.upscaler = KzgsUpscaler::Off;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	previous = stats();
	bool reopenedWhileOff = false;
	kzgs::RunOnGSThread([&] { reopenedWhileOff = kzgs::ProviderTestReopenDevice(); });
	check(reopenedWhileOff, "device can be recreated while the provider is off");
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::Off, 0, KzgsUpscalerReason::Disabled);
	check(stats().evaluates == previous.evaluates, "device recreation does not reactivate a disabled provider");
	std::vector<uint8_t> baseline, fallback;
	ReadPresented(baseline, 1003, 757);
	check(!providerCapture.empty() && providerCapture != baseline,
		"presented provider capture differs from the native fallback");
	cfg.upscaler = KzgsUpscaler::DLSS;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	setMode(MOCK_EVALUATE_FAILURE);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::DispatchFailed);
	if (ReadPresented(fallback, 1003, 757))
		check(!baseline.empty() && fallback == baseline, "dispatch failure preserves native presentation");
	setMode(0);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);

	previous = stats();
	ResizeTestWindowUnconstrained(hwnd, 1101, 801);
	kzgsResize(1101, 801);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);
	const KzgsMockStats resized = stats();
	check(resized.last_reset_reason == KZGS_PROVIDER_RESET_RESIZE &&
		(resized.last_output_width != previous.last_output_width ||
			resized.last_output_height != previous.last_output_height), "resize recreates provider output");

	kzgsReset();
	PresentFrames(scene, 2, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);
	check(stats().last_reset_reason == KZGS_PROVIDER_RESET_GAME, "GS reset recreates temporal history");

	setMode(MOCK_DEVICE_LOST);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::DeviceLost);
	previous = stats();
	setMode(0);
	PresentFrames(scene, 1, 3);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 0, KzgsUpscalerReason::DeviceLost);
	check(stats().evaluates == previous.evaluates, "lost device is not retried");
	cfg.upscalerSharpening = 1;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::DLSS, 1, KzgsUpscalerReason::DeviceLost);
	bool reopened = false;
	kzgs::RunOnGSThread([&] { reopened = kzgs::ProviderTestReopenDevice(); });
	check(reopened, "PCSX2 recreated the Vulkan device");
	if (reopened)
	{
		PresentFrames(scene, 2, 3);
		kzgsSync();
		CheckUpscalerStatus(KzgsUpscaler::DLSS, 1, KzgsUpscalerReason::Active, KzgsUpscaler::DLSS);
		check(stats().evaluates > previous.evaluates && stats().last_sharpening == 1,
			"provider resumes with updated settings on the recreated device");
	}

	kzgsClose();
	CheckUpscalerClosed();
	const KzgsMockStats closed = stats();
	check(closed.unsafe_destroys == 0 && closed.destroys == closed.creates,
		"all provider contexts retire after GPU completion");
	check(g_vulkan_validation_seen.load() && !g_vulkan_validation_unavailable.load() &&
		g_vulkan_validation_errors.load() == 0, "standard Vulkan validation is clean");
	kzgs::ProviderTestSetPath(nullptr);
	FreeLibrary(module);
	check(GetModuleHandleW(dllPath.c_str()) == nullptr, "provider DLL unloaded after close");
}

int main(int argc, char** argv)
{
	KzgsConfig cfg;
	bool show = false;
	bool vsync = false;
	bool golden_only = false;
	bool fsr1_validation = false;
	std::string synthetic_trace;
	std::string provider_validation;
	int frames = 4;
	int stress_uploads = 2600; // x 16 KB = ~41 MB of GIF data, more than the 32 MB ring
	for (int i = 1; i < argc; i++)
	{
		const std::string a = argv[i];
		if (a == "--renderer" && i + 1 < argc)
		{
			const std::string r = argv[++i];
			cfg.renderer = (r == "d3d12") ? KzgsRenderer::D3D12 : (r == "vulkan") ? KzgsRenderer::Vulkan :
				(r == "software") ? KzgsRenderer::Software : KzgsRenderer::D3D11;
		}
		else if (a == "--warp")
			cfg.useWarp = true;
		else if (a == "--golden")
			golden_only = g_check_golden = true;
		else if (a == "--fsr1-validation")
			fsr1_validation = true;
		else if (a == "--provider-validation" && i + 1 < argc)
			provider_validation = argv[++i];
		else if (a == "--write-synthetic-trace" && i + 1 < argc)
			synthetic_trace = argv[++i];
		else if (a == "--out" && i + 1 < argc)
			g_output_dir = argv[++i];
		else if (a == "--raw-gif") // negative check: self-contained packet phase must then fail
			cfg.selfContainedGifPackets = false;
		else if (a == "--vsync")
			vsync = true;
		else if (a == "--show")
			show = true;
		else if (a == "--frames" && i + 1 < argc)
			frames = std::max(1, std::atoi(argv[++i]));
		else if (a == "--stress-uploads" && i + 1 < argc)
			stress_uploads = std::max(0, std::atoi(argv[++i]));
	}
	static const char* names[] = {"D3D11", "D3D12", "Vulkan", "Software"};
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (!synthetic_trace.empty())
	{
		SetupNTSC640x448();
		const std::vector<uint8_t> scene = BuildScene();
		FILE* file = std::fopen(synthetic_trace.c_str(), "wb");
		if (!file)
			return 2;
		bool ok = true;
		for (uint32_t frame = 0; frame < 3; ++frame)
		{
			const uint32_t gif[] = {1, 3, static_cast<uint32_t>(scene.size())};
			const uint32_t vsyncRecord[] = {2, frame & 1, sizeof(g_regs)};
			SetPriv(0x1000, (frame & 1) ? (1ull << 13) : 0);
			ok = ok && std::fwrite(gif, sizeof(gif), 1, file) == 1 &&
				std::fwrite(scene.data(), scene.size(), 1, file) == 1 &&
				std::fwrite(vsyncRecord, sizeof(vsyncRecord), 1, file) == 1 &&
				std::fwrite(g_regs, sizeof(g_regs), 1, file) == 1;
		}
		ok = (std::fclose(file) == 0) && ok;
		std::printf("%s: synthetic replay trace\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
	std::error_code output_error;
	std::filesystem::create_directories(g_output_dir, output_error);
	if (output_error)
	{
		std::printf("FAIL: cannot create output directory %s: %s\n", g_output_dir.c_str(), output_error.message().c_str());
		return 2;
	}

	HWND hwnd = CreateTestWindow(show);
	if (!hwnd)
	{
		std::printf("FAIL: CreateWindow\n");
		return 2;
	}

	kzgsSetLogCallback(&Log);
	SetupNTSC640x448();
	if (!provider_validation.empty())
	{
		CheckProviderVulkan(hwnd, provider_validation, BuildScene());
		DestroyWindow(hwnd);
		std::printf("%s: provider renderer integration\n", g_failures ? "FAIL" : "PASS");
		return g_failures ? 1 : 0;
	}
	cfg.vsync = vsync;
	cfg.upscale = 1;
	cfg.bilinearPresent = false;
	cfg.debugDevice = fsr1_validation;

	std::string err;
	std::printf("== open %s%s\n", names[int(cfg.renderer)], cfg.useWarp ? " (WARP)" : "");
	const kz::host::NativeWindow native_window = kz::host::NativeWindow::win32(hwnd);
	CheckUpscalerClosed();
	KzgsConfig invalid = cfg;
	invalid.upscalerSharpening = 101;
	if (kzgsOpen(native_window, invalid, g_regs, &err) || err.find("upscaler sharpening") == std::string::npos)
	{
		std::printf("  FAIL invalid upscaler sharpening accepted at open\n");
		g_failures++;
		if (kzgsIsOpen())
			kzgsClose();
	}
	invalid = cfg;
	invalid.upscaler = static_cast<KzgsUpscaler>(999);
	if (kzgsOpen(native_window, invalid, g_regs, &err) || err.find("upscaler must") == std::string::npos)
	{
		std::printf("  FAIL invalid upscaler provider accepted at open\n");
		g_failures++;
		if (kzgsIsOpen())
			kzgsClose();
	}
	bool opened = kzgsOpen(native_window, cfg, g_regs, &err);
	if (!opened && !cfg.useWarp && cfg.renderer != KzgsRenderer::Vulkan)
	{
		std::printf("hardware %s device failed (%s); retrying on WARP\n", names[int(cfg.renderer)], err.c_str());
		cfg.useWarp = true;
		opened = kzgsOpen(native_window, cfg, g_regs, &err);
	}
	if (!opened)
	{
		std::printf("FAIL: kzgsOpen: %s\n", err.c_str());
		return 3;
	}
	std::printf("opened %s on %s adapter\n", names[int(cfg.renderer)], cfg.useWarp ? "WARP" : "default");
	CheckUpscalerStatus(KzgsUpscaler::Off, 0, KzgsUpscalerReason::Disabled);

	const std::vector<uint8_t> scene = BuildScene();
	std::printf("== 1x (%d frames, PATH3 packet %zu qwords)\n", frames, scene.size() / 16);
	RenderAndCheck(scene, frames, 1, "out.png");

	std::vector<uint8_t> baseline_backbuffer;
	const auto capture_backbuffer = [&](std::vector<uint8_t>& pixels) {
		kzgsBurstArm(1, -1, -1);
		PresentFrames(scene, frames, 3);
		kzgsSync();
		std::vector<KzgsBurstFrame> captured;
		if (!kzgsBurstTake(captured) || captured.size() != 1 || captured[0].rgba.empty())
		{
			std::printf("  FAIL upscaler fallback backbuffer capture\n");
			g_failures++;
			return false;
		}
		pixels = std::move(captured[0].rgba);
		return true;
	};
	// Keep swap-chain capture separate from the full test's later renderer-switch stress.
	if (golden_only && cfg.renderer != KzgsRenderer::Vulkan)
	{
		cfg.casSharpness = 30;
		kzgsUpdateConfig(cfg);
		kzgsSync();
		capture_backbuffer(baseline_backbuffer);
	}
	for (const KzgsUpscaler provider : {KzgsUpscaler::FSR1, KzgsUpscaler::DLSS, KzgsUpscaler::XeSS})
	{
		cfg.upscaler = provider;
		cfg.upscalerSharpening = 100;
		kzgsUpdateConfig(cfg);
		kzgsSync();
		CheckUpscalerStatus(provider, 100,
			provider == KzgsUpscaler::FSR1 && cfg.renderer == KzgsRenderer::Vulkan ?
				KzgsUpscalerReason::WaitingForFrame :
			cfg.renderer == KzgsRenderer::Vulkan && provider != KzgsUpscaler::FSR1 ?
				KzgsUpscalerReason::DeviceRecreationRequired : KzgsUpscalerReason::NotImplemented);
		if (golden_only && provider == KzgsUpscaler::FSR1 && cfg.renderer != KzgsRenderer::Vulkan)
		{
			std::vector<uint8_t> fallback_backbuffer;
			if (capture_backbuffer(fallback_backbuffer) && fallback_backbuffer != baseline_backbuffer)
			{
				std::printf("  FAIL unavailable upscaler changed presented pixels or CAS\n");
				g_failures++;
			}
		}
	}
	invalid = cfg;
	invalid.upscalerSharpening = -1;
	kzgsUpdateConfig(invalid);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::XeSS, 100, cfg.renderer == KzgsRenderer::Vulkan ?
		KzgsUpscalerReason::DeviceRecreationRequired : KzgsUpscalerReason::NotImplemented);
	cfg.upscaler = KzgsUpscaler::Off;
	cfg.upscalerSharpening = 75;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::Off, 75, KzgsUpscalerReason::Disabled);
	cfg.upscalerSharpening = 0;
	cfg.casSharpness = 0;
	kzgsUpdateConfig(cfg);
	kzgsSync();
	CheckUpscalerStatus(KzgsUpscaler::Off, 0, KzgsUpscalerReason::Disabled);
	if (fsr1_validation && cfg.renderer == KzgsRenderer::Vulkan)
		CheckFsr1Vulkan(hwnd, cfg, scene, frames);

	if (golden_only && cfg.renderer == KzgsRenderer::Software)
	{
		kzgsClose();
		CheckUpscalerClosed();
		DestroyWindow(hwnd);
		std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
		return g_failures ? 1 : 0;
	}

	std::printf("== 2x upscale\n");
	cfg.upscale = 2;
	kzgsUpdateConfig(cfg);
	RenderAndCheck(scene, frames, 2, "out_2x.png");
	if (golden_only)
	{
		kzgsClose();
		CheckUpscalerClosed();
		DestroyWindow(hwnd);
		if (fsr1_validation && g_vulkan_validation_errors.load(std::memory_order_relaxed) != 0)
		{
			std::printf("  FAIL Vulkan validation reported errors during final teardown\n");
			++g_failures;
		}
		std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
		return g_failures ? 1 : 0;
	}

	if (cfg.renderer == KzgsRenderer::D3D12)
	{
		std::printf("== D3D12 resize 800x600\n");
		ResizeTestWindow(hwnd, 800, 600);
		kzgsResize(800, 600);
		RenderAndCheckBackbuffer(scene, frames, 800, 600, "out_resize.png");
		CheckResolutionInfo(1280, 896, 800, 600);

		struct OutputCase
		{
			int width;
			int height;
			const char* image;
		};
		static constexpr OutputCase output_matrix[] = {
			{1280, 720, "out_720p.png"},
			{1920, 1080, "out_1080p.png"},
			{2560, 1440, "out_1440p.png"},
			{3840, 2160, "out_4k.png"},
		};
		for (const OutputCase& output : output_matrix)
		{
			std::printf("== D3D12 output matrix %dx%d (fixed 2x render)\n", output.width, output.height);
			ResizeTestWindowUnconstrained(hwnd, output.width, output.height);
			kzgsResize(output.width, output.height);
			RenderAndCheckBackbuffer(scene, frames, output.width, output.height, output.image);
			CheckResolutionInfo(1280, 896, output.width, output.height);
		}
		RestoreTestWindowStyle(hwnd);

		std::printf("== D3D12 minimize/restore\n");
		ShowWindow(hwnd, SW_MINIMIZE);
		PumpMessages();
		kzgsResize(0, 0); // minimized windows retain the last valid swap-chain size
		ShowWindow(hwnd, SW_RESTORE);
		ResizeTestWindow(hwnd, 640, 448);
		kzgsResize(640, 448);
		RenderAndCheckBackbuffer(scene, frames, 640, 448, "out_restore.png");
		CheckResolutionInfo(1280, 896, 640, 448);

		std::printf("== D3D12 device-loss recovery\n");
		if (!kzgsDebugInjectDeviceLoss())
		{
			std::printf("  FAIL could not inject D3D12 device loss\n");
			g_failures++;
		}
		else
		{
			PresentFrames(scene, 2, 3); // first present reopens the device; second repopulates it
			kzgsSync();
			RenderAndCheckBackbuffer(scene, frames, 640, 448, "out_device_recovery.png");
			CheckResolutionInfo(1280, 896, 640, 448);
		}
	}

	// Ring stress over PATH2: >32 MB of IMAGE uploads to off-screen VRAM (wraps the ring several times over), split
	// into 7-qword chunks so GIF packets straddle transfer boundaries like a DMA-chain-driven arbiter would produce,
	// with a vsync every 100 uploads. Then a frame whose last packet uploads a visible magenta block over the scene.
	// (Uploads keep GPU work small: thousands of full-screen fills per frame trip the 2 s GPU watchdog on an iGPU.)
	{
		std::printf("== ring stress (PATH2, %d x 16 KB uploads, 7-qword chunks, 2x)\n", stress_uploads);
		// Chunked packets need the raw-stream GIF mode (a packet continues in the next kzgsGifTransfer call).
		KzgsConfig raw = cfg;
		raw.selfContainedGifPackets = false;
		kzgsUpdateConfig(raw);
		const DWORD t0 = GetTickCount();
		size_t bytes = 0;
		for (int i = 0; i < stress_uploads; i++)
		{
			const uint32_t c = 0x80000000u | (uint32_t(i) * 2654435761u & 0x00FFFFFFu);
			const std::vector<uint8_t> up = BuildUpload(9216 + (i % 8) * 64, 1, 0, 0, 64, 64, c);
			SendChunked(2, up, 7);
			bytes += up.size();
			if ((i % 100) == 99)
				kzgsVsync(0);
		}
		kzgsSync();
		std::printf("  queued+drained %.1f MB in %lu ms\n", bytes / (1024.0 * 1024.0), GetTickCount() - t0);

		std::vector<uint8_t> frame = scene;
		const std::vector<uint8_t> magenta = BuildUpload(0, 10, 560, 400, 64, 32, 0x80FF00FFu);
		frame.insert(frame.end(), magenta.begin(), magenta.end());
		RenderAndCheck(frame, 2, 2, "out_stress.png", 2);
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 590, 415, 255, 0, 255, "uploaded");
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 623, 431, 255, 0, 255, "upload edge");
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 624, 432, kBackground.r, kBackground.g, kBackground.b, "past upload");
		kzgsUpdateConfig(cfg);
	}

	// Self-contained packet mode (default), as PS2Recomp's GIF arbiter feeds it: a VIF DIRECT ends with an IMAGE tag
	// whose data arrives in a later DMA chunk, and the runtime forwards that data re-wrapped in its own IMAGE tag.
	// Packet A = BITBLTBUF/TRXPOS/TRXREG/TRXDIR + IMAGE tag (NLOOP=n, EOP=0) and no data; packet B = IMAGE tag
	// (NLOOP=n, EOP=1) + data. kzgs must drop A's open tag; otherwise B's tag is eaten as pixel data and B's last qword
	// becomes a garbage GIFtag.
	{
		std::printf("== self-contained packets (dangling IMAGE tag + re-wrapped continuation over PATH2, 2x)\n");
		const std::vector<uint8_t> up = BuildUpload(0, 10, 480, 8, 64, 24, 0x80FFFF00u); // cyan
		std::vector<uint8_t> a(up.begin(), up.begin() + 96);
		a[80 + 1] &= 0x7F; // clear EOP on the IMAGE tag
		std::vector<uint8_t> b(up.begin() + 80, up.end());
		for (int f = 0; f < 2; f++)
		{
			kzgsGifTransfer(3, scene.data(), uint32_t(scene.size() / 16));
			kzgsGifTransfer(2, a.data(), uint32_t(a.size() / 16));
			kzgsGifTransfer(2, b.data(), uint32_t(b.size() / 16));
			SetPriv(0x1000, (f & 1) ? (1ull << 13) : 0);
			kzgsVsync(f & 1);
		}
		RenderAndCheck(scene, 0, 2, "out_packets.png");
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 512, 20, 0, 255, 255, "rewrapped");
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 543, 31, 0, 255, 255, "rewrap edge");
		CheckPixel(g_last_rgba, g_last_w, g_last_h, 2, 512, 32, kBackground.r, kBackground.g, kBackground.b, "below rewrap");
	}

	// Renderer switch on the same window through kzgsUpdateConfig (GSreopen path), scene sent over PATH1.
	{
		KzgsConfig sw = cfg;
		sw.upscale = 1;
		sw.renderer = (cfg.renderer == KzgsRenderer::D3D11) ? KzgsRenderer::D3D12 : KzgsRenderer::D3D11;
		std::printf("== switch renderer %s -> %s (scene over PATH1)\n", names[int(cfg.renderer)], names[int(sw.renderer)]);
		kzgsUpdateConfig(sw);
		RenderAndCheck(scene, frames, 1, "out_switch.png", 1);
	}

	kzgsClose();
	CheckUpscalerClosed();
	DestroyWindow(hwnd);

	std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
