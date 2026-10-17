// kzgs shim: the Host:: interface PCSX2's GS calls back into (window, OSD, errors, settings, translation).
// SPDX-License-Identifier: GPL-3.0+

#include "kzgs_internal.h"

#include "GS/GS.h"
#include "Host.h"

#include "common/Console.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/WindowInfo.h"

#include <cstdarg>
#include <mutex>

static kz::host::NativeWindow s_window;
static std::mutex s_error_lock;
static std::string s_last_error;

void kzgs::SetRenderWindow(const kz::host::NativeWindow& window)
{
	s_window = window;
}

void kzgs::ClearLastError()
{
	std::lock_guard lock(s_error_lock);
	s_last_error.clear();
}

std::string kzgs::GetLastError()
{
	std::lock_guard lock(s_error_lock);
	return s_last_error;
}

static void RecordError(std::string_view title, std::string_view message)
{
	Console.Error(fmt::format("{}: {}", title, message));
	std::lock_guard lock(s_error_lock);
	if (!s_last_error.empty())
		s_last_error += "\n";
	s_last_error += fmt::format("{}: {}", title, message);
}

// ---- Window ---------------------------------------------------------------------------------------------------------

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
	WindowInfo wi;
	if (!s_window)
	{
		wi.type = WindowInfo::Type::Surfaceless;
		return wi;
	}

	if (s_window.type != kz::host::NativeWindowType::Win32)
	{
		RecordError("Unsupported native window", "The current kzgs backends require a Win32 window");
		return std::nullopt;
	}

	kz::host::WindowMetrics metrics;
	try
	{
		metrics = kz::host::queryWindowMetrics(s_window);
	}
	catch (const kz::host::Error& error)
	{
		RecordError("Native window query failed", error.what());
		return std::nullopt;
	}
	wi.type = WindowInfo::Type::Win32;
	wi.window_handle = s_window.window;
	wi.surface_width = metrics.width;
	wi.surface_height = metrics.height;
	wi.surface_scale = metrics.scale;
	if (const std::optional<float> rr = WindowInfo::QueryRefreshRateForWindow(wi); rr.has_value())
		wi.surface_refresh_rate = rr.value();
	return wi;
}

void Host::ReleaseRenderWindow() {}
void Host::BeginPresentFrame() {}
bool Host::IsFullscreen() { return false; }
void Host::SetFullscreen(bool enabled) {}
void Host::OnCaptureStarted(const std::string& filename) {}
void Host::OnCaptureStopped() {}

// ---- OSD / errors ---------------------------------------------------------------------------------------------------
// PCSX2 shows these as on-screen toasts; kzgs sends them to the log instead.

void Host::AddOSDMessage(std::string message, float duration)
{
	Console.WriteLn(fmt::format("OSD: {}", message));
}

void Host::AddKeyedOSDMessage(std::string key, std::string message, float duration)
{
	Console.WriteLn(fmt::format("OSD [{}]: {}", key, message));
}

void Host::AddIconOSDMessage(std::string key, const char* icon, const std::string_view message, float duration)
{
	Console.WriteLn(fmt::format("OSD [{}]: {}", key, message));
}

void Host::RemoveKeyedOSDMessage(std::string key) {}
void Host::ClearOSDMessages() {}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	RecordError(title, message);
}

void Host::ReportFormattedErrorAsync(const std::string_view title, const char* format, ...)
{
	std::va_list ap;
	va_start(ap, format);
	const std::string message = StringUtil::StdStringFromFormatV(format, ap);
	va_end(ap);
	RecordError(title, message);
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	Console.WriteLn(fmt::format("{}: {}", title, message));
}

void Host::RunOnCPUThread(std::function<void()> function, bool block)
{
	// kzgs has no emulated CPU thread; the only GS callers are hotkey handlers, which kzgs never triggers.
	function();
}

// ---- Translation (identity) -----------------------------------------------------------------------------------------

const char* Host::TranslateToCString(const std::string_view context, const std::string_view msg)
{
	// Callers pass string literals, which are NUL-terminated.
	return msg.data();
}

std::string_view Host::TranslateToStringView(const std::string_view context, const std::string_view msg)
{
	return msg;
}

std::string Host::TranslateToString(const std::string_view context, const std::string_view msg)
{
	return std::string(msg);
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
	std::string ret(msg);
	StringUtil::ReplaceAll(&ret, "%n", std::to_string(count));
	return ret;
}

// ---- Settings (always the default; kzgs is configured through KzgsConfig) -------------------------------------------

std::string Host::GetStringSettingValue(const char* section, const char* key, const char* default_value)
{
	return default_value ? std::string(default_value) : std::string();
}

bool Host::GetBoolSettingValue(const char* section, const char* key, bool default_value)
{
	return default_value;
}

bool Host::GetBaseBoolSettingValue(const char* section, const char* key, bool default_value)
{
	return default_value;
}
