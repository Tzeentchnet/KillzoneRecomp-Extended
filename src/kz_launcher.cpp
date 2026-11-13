#include "kz_launcher.h"

#include "kz_iso.h"
#include "kz_input.h"
#include "kz_widescreen.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    // KZ_LAUNCHER_TAB=<name>: opens that tab (screenshots of other tabs in the self-test).
    ImGuiTabItemFlags forcedTab(const char *name)
    {
        static const char *forced = std::getenv("KZ_LAUNCHER_TAB");
        return forced && _stricmp(forced, name) == 0 ? ImGuiTabItemFlags_SetSelected : 0;
    }
}

namespace
{
    constexpr int kWidth = 900;
    constexpr int kHeight = 760;

    struct Resolution
    {
        int w, h;
    };

    std::vector<Resolution> desktopResolutions()
    {
        std::vector<Resolution> out;
        int count = 0;
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count))
        {
            for (int i = 0; i < count; ++i)
            {
                Resolution r{modes[i]->w, modes[i]->h};
                if (std::none_of(out.begin(), out.end(), [&](const Resolution &o) { return o.w == r.w && o.h == r.h; }))
                    out.push_back(r);
            }
            SDL_free(modes);
        }
        if (out.empty())
            out = {{1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 720}};
        return out;
    }

    // Charcoal + Helghast red accent.
    void applyStyle()
    {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 0.0f;
        s.FrameRounding = 4.0f;
        s.GrabRounding = 4.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 8);
        ImVec4 *c = s.Colors;
        const ImVec4 bg(0.105f, 0.105f, 0.11f, 1.0f), panel(0.15f, 0.15f, 0.16f, 1.0f), accent(0.78f, 0.20f, 0.14f, 1.0f),
            accentHi(0.90f, 0.28f, 0.20f, 1.0f), text(0.92f, 0.91f, 0.89f, 1.0f), dim(0.60f, 0.59f, 0.57f, 1.0f);
        c[ImGuiCol_WindowBg] = bg;
        c[ImGuiCol_ChildBg] = panel;
        c[ImGuiCol_PopupBg] = panel;
        c[ImGuiCol_Text] = text;
        c[ImGuiCol_TextDisabled] = dim;
        c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.21f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.26f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.29f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.23f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.31f, 1.0f);
        c[ImGuiCol_ButtonActive] = accent;
        c[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.25f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.31f, 1.0f);
        c[ImGuiCol_HeaderActive] = accent;
        c[ImGuiCol_CheckMark] = accentHi;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentHi;
        c[ImGuiCol_Tab] = panel;
        c[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.30f, 0.31f, 1.0f);
        c[ImGuiCol_TabSelected] = accent;
        c[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.26f, 1.0f);
    }

    struct LauncherState
    {
        KzConfig &cfg;
        std::vector<Resolution> resolutions;
        KzDiscCheck disc;
        std::string checkedPath = std::string(1, ''); // sentinel: forces a check on the first frame
        std::mutex dialogMutex;
        std::string pickedPath; // set by the async file dialog callback
        bool play = false;
        bool quit = false;
    };

    void applyValidatedGraphicsPreset(KzConfig &cfg)
    {
        cfg.renderer = KzRenderer::D3D12;
        cfg.vsync = true;
        cfg.upscale = 0;
        cfg.upscaler = KzgsUpscaler::Off;
        cfg.upscalerSharpening = 0;
        cfg.fsr3InputMode = KzgsFsr3InputMode::Strict;
        cfg.aspect = KzAspect::Widescreen16x9;
        cfg.anisotropy = 16;
        cfg.mipmapMode = KzMipmapMode::HighestDetail;
        cfg.fxaa = false;
        cfg.smaa = false;
        cfg.textureReplacement = false;
        cfg.bilinear = true;
        cfg.sharpScaling = true;
        cfg.sharpen = 30;
        cfg.noiseFilter = false;
        cfg.motionBlur = false;
        cfg.glow = false;
        cfg.lensBlur = false;
        cfg.renderDistance = 2.0f;
        cfg.lodScale = 1.0f;
    }

    void applyFsr1QualityGraphicsPreset(KzConfig &cfg)
    {
        cfg.renderer = KzRenderer::Vulkan;
        cfg.upscale = 3;
        cfg.upscaler = KzgsUpscaler::FSR1;
        cfg.upscalerSharpening = 15;
        cfg.fsr3InputMode = KzgsFsr3InputMode::Strict;
        cfg.anisotropy = 16;
        cfg.mipmapMode = KzMipmapMode::PS2;
        cfg.fxaa = false;
        cfg.smaa = false;
        cfg.bilinear = true;
        cfg.sharpScaling = true;
        cfg.sharpen = 0;
    }

    // Game rate for external frame generation (Lossless Scaling, AFMF, Smooth Motion): half the display refresh so a
    // fixed 2x multiplier fills it, but never below 60 (frame generation needs a high base rate; slower displays use an
    // adaptive multiplier instead). See docs/frame-generation.md.
    int externalFrameGenerationFpsLimit(int refreshHz)
    {
        return std::max(60, refreshHz / 2);
    }

    void applyExternalFrameGenerationPreset(KzConfig &cfg, int refreshHz)
    {
        cfg.windowMode = KzWindowMode::Borderless; // the tools capture a window; exclusive fullscreen is not captured
        cfg.vsync = false;                         // the frame-generation tool owns synchronization
        cfg.fpsLimit = externalFrameGenerationFpsLimit(refreshHz);
        // Even frame pacing is left unchanged: measured on SCES-52004 it helped when the game ran below its limit
        // but was unreliable at 60 fps, which this preset picks for 120 Hz displays (docs/frame-generation.md).
    }

    int displayRefreshHz(SDL_Window *window)
    {
        const SDL_DisplayID display = window ? SDL_GetDisplayForWindow(window) : SDL_GetPrimaryDisplay();
        if (const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode(display) : nullptr)
            if (mode->refresh_rate > 0.0f)
                return static_cast<int>(std::lround(mode->refresh_rate));
        return 60;
    }

    void onFileChosen(void *userdata, const char *const *files, int)
    {
        auto *st = static_cast<LauncherState *>(userdata);
        if (files && files[0])
        {
            std::lock_guard<std::mutex> lock(st->dialogMutex);
            st->pickedPath = files[0];
        }
    }

    template <typename E>
    bool enumCombo(const char *label, E &value, const std::vector<std::pair<E, const char *>> &items)
    {
        const char *current = items.front().second;
        for (const auto &[v, n] : items)
            if (v == value)
                current = n;
        bool changed = false;
        if (ImGui::BeginCombo(label, current))
        {
            for (const auto &[v, n] : items)
                if (ImGui::Selectable(n, v == value))
                {
                    value = v;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool upscalerCombo(KzgsUpscaler &value)
    {
        const char *current = "Off";
        switch (value)
        {
        case KzgsUpscaler::Off: current = "Off"; break;
        case KzgsUpscaler::FSR1: current = "AMD FSR 1"; break;
        case KzgsUpscaler::DLSS: current = "NVIDIA DLSS (unavailable)"; break;
        case KzgsUpscaler::XeSS: current = "Intel XeSS (unavailable)"; break;
        case KzgsUpscaler::FSR3: current = "AMD FSR 3 SR (experimental)"; break;
        }

        bool changed = false;
        if (ImGui::BeginCombo("##presentation-upscaler", current))
        {
            if (ImGui::Selectable("Off", value == KzgsUpscaler::Off))
            {
                value = KzgsUpscaler::Off;
                changed = true;
            }
            if (ImGui::Selectable("AMD FSR 1 (Vulkan)", value == KzgsUpscaler::FSR1))
            {
                value = KzgsUpscaler::FSR1;
                changed = true;
            }
            ImGui::Separator();
            if (ImGui::Selectable("AMD FSR 3 SR (Vulkan, experimental)",
                                  value == KzgsUpscaler::FSR3))
            {
                value = KzgsUpscaler::FSR3;
                changed = true;
            }
            ImGui::BeginDisabled();
            ImGui::Selectable("NVIDIA DLSS (unavailable)", value == KzgsUpscaler::DLSS);
            ImGui::Selectable("Intel XeSS (unavailable)", value == KzgsUpscaler::XeSS);
            ImGui::EndDisabled();
            ImGui::EndCombo();
        }
        return changed;
    }

    bool fsr3InputModeControlEnabled(const KzConfig &cfg)
    {
        return cfg.renderer == KzRenderer::Vulkan && cfg.upscaler == KzgsUpscaler::FSR3;
    }

    bool providerSharpeningControlEnabled(const KzConfig &cfg)
    {
        return cfg.upscaler == KzgsUpscaler::FSR1 || cfg.upscaler == KzgsUpscaler::FSR3;
    }

    // ---- layout helpers: every setting is one row, "label + one-line explanation" on the left, control on the right
    constexpr float kLabelWidth = 380.0f;

    bool beginRows(const char *id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None))
            return false;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    // Starts a row and leaves the cursor in the control column, with the next item filling its width.
    void row(const char *label, const char *help = nullptr)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (help)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kLabelWidth - 16.0f);
            ImGui::TextDisabled("%s", help);
            ImGui::PopTextWrapPos();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Field of view row: a slider in horizontal degrees at 16:9 and a button back to the game's own value (stored as 0).
    void fovRow(const char *label, const char *help, float &value, float native, float lo, float hi)
    {
        row(label, help);
        ImGui::PushID(label);
        const ImGuiStyle &style = ImGui::GetStyle();
        const float buttonWidth = ImGui::CalcTextSize("Original").x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(-(buttonWidth + style.ItemSpacing.x));
        float v = value > 0.0f ? value : native;
        const bool original = value <= 0.0f;
        if (ImGui::SliderFloat("##v", &v, lo, hi, original ? "%.0f (original)" : "%.0f"))
            value = std::fabs(v - native) < 0.5f ? 0.0f : std::round(v);
        ImGui::SameLine();
        ImGui::BeginDisabled(original);
        if (ImGui::Button("Original"))
            value = 0.0f;
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    // On/off row; `onText` / `offText` say what each state means.
    void toggleRow(const char *label, const char *help, bool &value, const char *onText = "On", const char *offText = "Off")
    {
        row(label, help);
        ImGui::PushID(label);
        ImGui::Checkbox("##v", &value);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(value ? onText : offText);
        ImGui::PopID();
    }

    void section(const char *title)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.35f, 1.0f));
        ImGui::SeparatorText(title);
        ImGui::PopStyleColor();
    }

    // Scrolling body of a tab: stops above the footer, so long tabs never run under the buttons.
    bool beginTabBody(const char *id)
    {
        const float footer = 64.0f;
        return ImGui::BeginChild(id, ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - footer)), ImGuiChildFlags_None);
    }

    void drawUi(LauncherState &st, SDL_Window *window)
    {
        KzConfig &cfg = st.cfg;
        {
            std::lock_guard<std::mutex> lock(st.dialogMutex);
            if (!st.pickedPath.empty())
            {
                cfg.isoPath = st.pickedPath;
                st.pickedPath.clear();
            }
        }
        if (cfg.isoPath != st.checkedPath)
        {
            st.disc = kzCheckDisc(cfg.isoPath);
            st.checkedPath = cfg.isoPath;
        }

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("KILLZONE");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("  PC port - settings");
        ImGui::Separator();

        // Disc
        ImGui::TextUnformatted("Game disc (your Killzone USA or Europe image)");
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "%s", cfg.isoPath.c_str());
        ImGui::SetNextItemWidth(-110);
        if (ImGui::InputText("##iso", buf, sizeof(buf)))
            cfg.isoPath = buf;
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(100, 0)))
        {
            static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso"}, {"All files", "*"}};
            SDL_ShowOpenFileDialog(onFileChosen, &st, window, filters, 2, nullptr, false);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, st.disc.ok ? ImVec4(0.45f, 0.80f, 0.45f, 1) : ImVec4(0.90f, 0.45f, 0.35f, 1));
        ImGui::TextWrapped("%s", st.disc.message.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginTabBar("tabs"))
        {
            if (ImGui::BeginTabItem("Display", nullptr, forcedTab("Display")))
            {
                if (beginTabBody("display"))
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Presets:");
                    ImGui::SameLine();
                    if (ImGui::Button("External frame generation"))
                        applyExternalFrameGenerationPreset(cfg, displayRefreshHz(window));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("For Lossless Scaling, AMD AFMF or NVIDIA Smooth Motion:\n"
                                          "borderless window, V-Sync off, and the game at half the monitor refresh\n"
                                          "(at least 60 fps). The HUD is interpolated too.");
                    if (beginRows("rows"))
                    {
                        row("Window mode");
                        enumCombo<KzWindowMode>("##mode", cfg.windowMode,
                                                {{KzWindowMode::Borderless, "Borderless fullscreen"},
                                                 {KzWindowMode::Fullscreen, "Exclusive fullscreen"},
                                                 {KzWindowMode::Windowed, "Windowed"}});
                        row("Resolution", "Desktop = your monitor's current resolution.");
                        std::string resLabel = cfg.width == 0 ? "Desktop" : std::to_string(cfg.width) + " x " + std::to_string(cfg.height);
                        if (ImGui::BeginCombo("##res", resLabel.c_str()))
                        {
                            if (ImGui::Selectable("Desktop", cfg.width == 0))
                                cfg.width = cfg.height = 0;
                            for (const Resolution &r : st.resolutions)
                            {
                                const std::string l = std::to_string(r.w) + " x " + std::to_string(r.h);
                                if (ImGui::Selectable(l.c_str(), cfg.width == r.w && cfg.height == r.h))
                                {
                                    cfg.width = r.w;
                                    cfg.height = r.h;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        row("Frame rate", "The game runs at this rate (the PS2 original ran at about 30).");
                        // 72 and 82 are half of 144 and 165 Hz, for 2x external frame generation.
                        static const int kFps[] = {0, 60, 72, 82, 120, 144, 165, 240, 360}; // 0 = monitor refresh
                        std::string fpsLabel = cfg.fpsLimit == 0 ? "Match monitor refresh" : std::to_string(cfg.fpsLimit) + " fps";
                        if (ImGui::BeginCombo("##fps", fpsLabel.c_str()))
                        {
                            for (int f : kFps)
                                if (ImGui::Selectable(f == 0 ? "Match monitor refresh" : (std::to_string(f) + " fps").c_str(), cfg.fpsLimit == f))
                                    cfg.fpsLimit = f;
                            ImGui::EndCombo();
                        }
                        toggleRow("V-Sync", "Removes tearing; adds a little input delay.", cfg.vsync);
                        toggleRow("Even frame pacing",
                                  "Experimental. Spaces out the catch-up frame after a hitch. Can help external frame "
                                  "generation (Lossless Scaling) when the game runs below its frame-rate limit; can "
                                  "hurt when it runs right at it. Usually unnecessary with V-Sync on.",
                                  cfg.framePacing);
                        toggleRow("Show FPS", "Frame-rate counter in the top-left corner while playing.", cfg.showFps);
                        row("Threaded graphics", "Faster, but can cause graphics corruption or crashes on some missions. "
                                                 "Auto: on for the US disc, off for the European disc.");
                        enumCombo<int>("##threaded", cfg.threadedGraphics,
                                       {{-1, "Auto"}, {1, "On (faster, less stable)"}, {0, "Off (stable, slower)"}});
                        toggleRow("Skip intro movie",
                                  "Skips the long intro after the Sony and Guerrilla logos. Story cutscenes still play.",
                                  cfg.skipLongIntro);
                        toggleRow("Skip logos", "Runs the Sony and Guerrilla logo screens at startup through in a moment.", cfg.skipLogos);
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Graphics", nullptr, forcedTab("Graphics")))
            {
                if (beginTabBody("graphics"))
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Presets:");
                    ImGui::SameLine();
                    if (ImGui::Button("Validated enhancements"))
                        applyValidatedGraphicsPreset(cfg);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Measured stock-game test setup: D3D12, V-Sync on, auto render scale, 16x anisotropy,\n"
                                          "base-mip detail, replacement textures off, clean PS2 effects,\n"
                                          "2x view distance and 30%% sharpening.");
                    ImGui::SameLine();
                    if (ImGui::Button("FSR 1 quality"))
                        applyFsr1QualityGraphicsPreset(cfg);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Vulkan, 3x internal rendering, FSR 1 with 15%% RCAS,\n"
                                          "PS2 mip transitions, 16x anisotropy and no additional CAS or AA.\n"
                                          "Designed for 1440p output.");
                    ImGui::SameLine();
                    if (ImGui::Button("Sharp and clean"))
                    {
                        cfg.fxaa = false; cfg.smaa = true; cfg.sharpScaling = true; cfg.bilinear = true; cfg.anisotropy = 16;
                        cfg.noiseFilter = false; cfg.motionBlur = false; cfg.glow = false; cfg.lensBlur = false; cfg.sharpen = 30;
                        cfg.upscaler = KzgsUpscaler::Off; cfg.upscalerSharpening = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Original PS2 look"))
                    {
                        cfg.fxaa = false; cfg.smaa = false; cfg.sharpScaling = false; cfg.bilinear = true;
                        cfg.mipmapMode = KzMipmapMode::PS2;
                        cfg.noiseFilter = true; cfg.motionBlur = true; cfg.glow = true; cfg.lensBlur = true;
                        cfg.renderDistance = 1.0f; cfg.lodScale = 1.0f; cfg.sharpen = 0; cfg.fov = 0.0f; cfg.weaponFov = 0.0f;
                        cfg.upscaler = KzgsUpscaler::Off; cfg.upscalerSharpening = 0;
                    }

                    section("Output");
                    if (beginRows("output"))
                    {
                        row("Renderer", "Choose the graphics API used to render and present the game.");
                        enumCombo<KzRenderer>("##renderer", cfg.renderer,
                                              {{KzRenderer::D3D11, "Direct3D 11"}, {KzRenderer::D3D12, "Direct3D 12"}, {KzRenderer::Vulkan, "Vulkan"}});
                        row("Internal render scale", "Integer GS render scale. Auto chooses the smallest whole scale that covers the output height.");
                        const char *scales[] = {"Auto (from output height)", "1x (448p target)", "2x (896p target)",
                                                "3x (1344p target)", "4x (1792p target)", "5x (2240p target)",
                                                "6x (2688p target)", "7x (3136p target)", "8x (3584p target)"};
                        ImGui::Combo("##upscale", &cfg.upscale, scales, IM_ARRAYSIZE(scales));
                        row("Aspect ratio",
                            "16:9 uses the game's native widescreen FOV and HUD. Ultrawide shows more at the sides while "
                            "keeping the HUD in the middle; menus and movies get side bars.");
                        enumCombo<KzAspect>("##aspect", cfg.aspect,
                                            {{KzAspect::Widescreen16x9, "16:9 widescreen (native)"},
                                             {KzAspect::Classic4x3, "4:3 original"},
                                             {KzAspect::Stretch, "Stretch to window"},
                                             {KzAspect::UltrawideAuto, "Ultrawide: match window (more view at the sides)"},
                                             {KzAspect::Ultrawide21x9, "Ultrawide 21:9"},
                                             {KzAspect::Ultrawide32x9, "Ultrawide 32:9"}});
                        row("Output scaling filter", "How the rendered picture is fitted to the output. This does not change internal render scale.");
                        int scaling = cfg.sharpScaling ? 0 : 1;
                        const char *scalings[] = {"Sharp (crisp pixels)", "Smooth (softer)"};
                        if (ImGui::Combo("##scaling", &scaling, scalings, 2))
                            cfg.sharpScaling = scaling == 0;
                        ImGui::EndTable();
                    }

                    section("Presentation upscaling");
                    if (beginRows("presentation-upscaling"))
                    {
                        row("Presentation upscaler",
                            "FSR 1 is available only on Vulkan. At 1440p, 3x internal rendering keeps FSR active "
                            "while reducing aliasing. FSR 3 Super Resolution is experimental and Vulkan-only; "
                            "DLSS and XeSS remain unavailable.");
                        upscalerCombo(cfg.upscaler);

                        row("Availability",
                            "The runtime confirms the effective provider after the first presented frame and falls "
                            "back safely if its requirements are not met.");
                        if (cfg.upscaler == KzgsUpscaler::FSR1 && cfg.renderer == KzRenderer::Vulkan)
                            ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.45f, 1.0f), "Ready (verified at game start)");
                        else if (cfg.upscaler == KzgsUpscaler::FSR1)
                            ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.35f, 1.0f), "Select Vulkan to enable FSR 1");
                        else if (cfg.upscaler == KzgsUpscaler::FSR3 &&
                                 cfg.renderer != KzRenderer::Vulkan)
                            ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.35f, 1.0f),
                                               "Select Vulkan to enable FSR 3 SR");
                        else if (cfg.upscaler == KzgsUpscaler::FSR3 &&
                                 cfg.fsr3InputMode == KzgsFsr3InputMode::Strict)
                            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.30f, 1.0f),
                                               "Strict mode intentionally fails closed: qualified inputs are unavailable");
                        else if (cfg.upscaler == KzgsUpscaler::FSR3)
                            ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.45f, 1.0f),
                                               "Experimental path enabled (verified at game start)");
                        else if (kzgsIsTemporalUpscaler(cfg.upscaler))
                            ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.35f, 1.0f), "Selected provider is unavailable");
                        else
                            ImGui::TextDisabled("Off");

                        row("Provider sharpening",
                            "RCAS sharpening applied by FSR 1 or FSR 3 SR. Start around 15%%. The separate "
                            "Sharpening control below applies another CAS pass, so avoid unintentionally stacking both.");
                        ImGui::BeginDisabled(!providerSharpeningControlEnabled(cfg));
                        ImGui::SliderInt("##fsr-sharpening", &cfg.upscalerSharpening, 0, 100,
                                         cfg.upscalerSharpening == 0 ? "Off" : "%d%%");
                        ImGui::EndDisabled();

                        row("FSR 3 input mode (experimental)",
                            "Strict requires qualified temporal inputs and fails closed. Experimental explicitly "
                            "allows approximated inputs; it is available only with Vulkan and FSR 3 SR.");
                        ImGui::BeginDisabled(!fsr3InputModeControlEnabled(cfg));
                        enumCombo<KzgsFsr3InputMode>(
                            "##fsr3-input-mode", cfg.fsr3InputMode,
                            {{KzgsFsr3InputMode::Strict, "Strict (qualified inputs only)"},
                             {KzgsFsr3InputMode::Experimental, "Experimental (approximated inputs)"}});
                        ImGui::EndDisabled();

                        row("Experimental input warning",
                            "FSR 3 experimental mode uses synthetic far-plane depth and color-derived Optical Flow "
                            "motion. The HUD and viewmodel remain unjittered, so edges, disocclusion, and ghosting "
                            "can be incorrect. This is Super Resolution only; it does not generate frames.");
                        if (cfg.upscaler == KzgsUpscaler::FSR3 &&
                            cfg.fsr3InputMode == KzgsFsr3InputMode::Experimental)
                            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.30f, 1.0f),
                                               "Approximate inputs active");
                        else
                            ImGui::TextDisabled("Not active");
                        ImGui::EndTable();
                    }

                    section("Field of view");
                    if (beginRows("fov"))
                    {
                        fovRow("Field of view", "Degrees across a 16:9 screen; ultrawide adds more at the sides. Zoom keeps its magnification.",
                               cfg.fov, kzFovNativeWorld(), 60.0f, 130.0f);
                        fovRow("Weapon field of view", "Higher shows more of the gun and arms (they look smaller). Lower brings the gun closer.",
                               cfg.weaponFov, kzFovNativeWeapon(), 50.0f, 120.0f);
                        ImGui::EndTable();
                    }

                    section("Anti-aliasing");
                    if (beginRows("aa"))
                    {
                        const bool smaaAvailable = kzRendererSupportsSmaa(cfg.renderer);
                        row("SMAA", smaaAvailable
                                        ? "Smooths jagged edges; costs a little fine texture detail."
                                        : "Unavailable on this renderer. Enable FXAA explicitly or raise internal resolution.");
                        ImGui::PushID("SMAA");
                        ImGui::BeginDisabled(!smaaAvailable);
                        ImGui::Checkbox("##v", &cfg.smaa);
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(smaaAvailable ? (cfg.smaa ? "On" : "Off")
                                                           : "Unavailable (Direct3D 11 only)");
                        ImGui::PopID();
                        toggleRow("FXAA", "Softens the whole picture slightly.", cfg.fxaa, "On (softer)", "Off");
                        ImGui::EndTable();
                    }

                    section("Textures");
                    if (beginRows("textures"))
                    {
                        row("Texture filtering");
                        int filtering = cfg.bilinear ? 0 : 1;
                        const char *filterings[] = {"Bilinear (as the game intends)", "Nearest (blocky pixels)"};
                        if (ImGui::Combo("##filtering", &filtering, filterings, 2))
                            cfg.bilinear = filtering == 0;
                        row("Anisotropic filtering",
                            "Improves eligible angled triangle textures. Paletted, sprite and manual-mipmap draws are unaffected.");
                        const int anisoValues[] = {0, 2, 4, 8, 16};
                        const char *aniso[] = {"Off", "2x", "4x", "8x", "16x"};
                        int anisoIdx = static_cast<int>(std::find(std::begin(anisoValues), std::end(anisoValues), cfg.anisotropy) - std::begin(anisoValues));
                        if (anisoIdx >= 5)
                            anisoIdx = 4;
                        if (ImGui::Combo("##aniso", &anisoIdx, aniso, 5))
                            cfg.anisotropy = anisoValues[anisoIdx];
                        row("Mipmap detail",
                            "PS2 transitions use authored lower-resolution mip levels. Highest detail always samples the base level.");
                        enumCombo<KzMipmapMode>("##mipmapping", cfg.mipmapMode,
                                               {{KzMipmapMode::PS2, "PS2 transitions"},
                                                {KzMipmapMode::HighestDetail, "Highest detail (base mip only)"}});
                        toggleRow("Replacement textures", "Loads texture packs from the textures folder.", cfg.textureReplacement);
                        ImGui::EndTable();
                    }

                    section("PS2 screen effects (off = cleaner picture)");
                    if (beginRows("effects"))
                    {
                        toggleRow("Motion blur", "Smears the previous frame over the current one.", cfg.motionBlur, "On (as on PS2)", "Off (clean)");
                        toggleRow("Film grain", "Moving noise over the whole picture.", cfg.noiseFilter, "On (as on PS2)", "Off (clean)");
                        toggleRow("Glow", "Soft halo around bright surfaces.", cfg.glow, "On (as on PS2)", "Off (clean)");
                        toggleRow("Rain-on-lens blur", "Blurs the view when rain hits the camera.", cfg.lensBlur, "On (as on PS2)", "Off (clean)");
                        ImGui::EndTable();
                    }

                    section("Detail");
                    if (beginRows("detail"))
                    {
                        row("View distance",
                            "Pushes the fog back. 2x is the supported maximum; farther views expose enemy spawns "
                            "and low-detail background geometry.");
                        ImGui::SliderFloat("##renderdist", &cfg.renderDistance, 1.0f, 2.0f, "%.1fx");
                        row("Model detail distance",
                            "Unavailable: elevated model LOD hangs Escape in interactive gameplay.");
                        cfg.lodScale = 1.0f;
                        ImGui::BeginDisabled();
                        ImGui::SliderFloat("##lod", &cfg.lodScale, 1.0f, 1.0f, "Unavailable (1x)");
                        ImGui::EndDisabled();
                        row("Sharpening", "Extra edge contrast on the final picture (30 % by default).");
                        ImGui::SliderInt("##sharpen", &cfg.sharpen, 0, 100, cfg.sharpen == 0 ? "Off" : "%d%%");
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls", nullptr, forcedTab("Controls")))
            {
                if (beginTabBody("controls"))
                {
                    section("Mouse");
                    if (beginRows("mouse"))
                    {
                        row("Sensitivity", "1.0 = 0.05 degrees per mouse count, same on both axes.");
                        ImGui::SliderFloat("##sens", &cfg.mouseSensitivity, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                        toggleRow("Invert vertical look", nullptr, cfg.invertY);
                        toggleRow("Aim", "Hold the zoom button to aim: right mouse button, or R3 on a controller.", cfg.holdAim, "Hold to aim", "Press to toggle (as on PS2)");
                        toggleRow("Sprint", "Left Shift / L3. Toggle: press once, sprint stops when you stop moving or press again. Also applies to controllers.",
                                  cfg.toggleSprint, "Press to toggle", "Hold to sprint (as on PS2)");
                        toggleRow("Keyboard prompts", "Shows your bound keys and mouse buttons while you use them. Controller input restores controller buttons.",
                                  cfg.buttonPrompts, "Auto (keys and mouse buttons)", "Always controller buttons");
                        ImGui::EndTable();
                    }
                    section("Controller");
                    if (beginRows("pad"))
                    {
                        row("Stick dead zone", "Controllers are detected automatically (PS2 layout).");
                        ImGui::SliderFloat("##deadzone", &cfg.stickDeadzone, 0.0f, 0.5f, "%.2f");
                        row("Stick range", "1.33 gives full-speed diagonals like a PS2 pad; 1.00 keeps the controller's raw circle.");
                        ImGui::SliderFloat("##range", &cfg.stickRange, 1.0f, 1.5f, "%.2f");
                        toggleRow("Controller glyphs",
                                  "Opt in to detected Xbox or Nintendo button labels. PlayStation, GameCube and unknown controllers keep the game's original PlayStation artwork.",
                                  cfg.controllerGlyphs, "Match detected controller", "PlayStation buttons (original)");
                        ImGui::EndTable();
                    }
                    section("Keys");
                    static const auto bindings = kzInputDescribeBindings(kzConfigPath());
                    if (ImGui::BeginTable("bindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
                    {
                        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        for (const auto &[key, action] : bindings)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(key.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextUnformatted(action.c_str());
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("To change keys, edit the [Bindings] section of killzone.ini.");
                    if (ImGui::Button("Open killzone.ini"))
                    {
                        if (kzSaveConfig(kzConfigPath(), cfg))
                        {
                            const std::string url = "file:///" + kzConfigPath().generic_string();
                            SDL_OpenURL(url.c_str());
                        }
                        else
                        {
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Killzone",
                                                     "Could not save killzone.ini. See killzone.log for details.",
                                                     window);
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio", nullptr, forcedTab("Audio")))
            {
                if (beginTabBody("audio"))
                {
                    if (beginRows("rows"))
                    {
                        row("Master volume", "Sound plays on the Windows default output device.");
                        ImGui::SliderInt("##volume", &cfg.masterVolume, 0, 100, "%d%%");
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Testing", nullptr, forcedTab("Testing")))
            {
                if (beginTabBody("testing"))
                {
                    section("Campaign");
                    if (beginRows("campaign"))
                    {
                        toggleRow("Unlock all campaign levels",
                                  "Every level, difficulty and animatic can be chosen with any profile. Your saves and progress are not changed.",
                                  cfg.unlockAllLevels, "On (all unlocked)", "Off (normal progression)");
                        ImGui::EndTable();
                    }
                    section("Start-up");
                    if (beginRows("startup"))
                    {
                        toggleRow("Skip intro",
                                  "Goes straight to the main menu: no logos, no intro movie. Known issue: loading a late "
                                  "chapter afterwards sometimes crashes (being investigated).",
                                  cfg.skipIntro,
                                  "On (straight to menu)", "Off (logos and intro)");
                        row("Start in", "Clicks through the menus into this chapter with the first profile on the memory "
                                        "card. On an empty isolated test card, the configured test profile is created "
                                        "through the game's front end. Skips the intro and unlocks all levels.");
                        const std::string startLabel = cfg.startLevel == 0 ? "Main menu (normal start)" : "Chapter " + std::to_string(cfg.startLevel);
                        if (ImGui::BeginCombo("##startlevel", startLabel.c_str()))
                        {
                            if (ImGui::Selectable("Main menu (normal start)", cfg.startLevel == 0))
                                cfg.startLevel = 0;
                            for (int level = 1; level <= 11; ++level)
                                if (ImGui::Selectable(("Chapter " + std::to_string(level)).c_str(), cfg.startLevel == level))
                                    cfg.startLevel = level;
                            ImGui::EndCombo();
                        }
                        row("Difficulty", "Used with Start in.");
                        ImGui::BeginDisabled(cfg.startLevel == 0);
                        const char *difficulties[] = {"Easy", "Normal", "Hard"};
                        ImGui::Combo("##startdifficulty", &cfg.startDifficulty, difficulties, 3);
                        ImGui::EndDisabled();
                        ImGui::EndTable();
                    }
                    section("Memory card");
                    if (beginRows("card"))
                    {
                        toggleRow("Test memory card",
                                  "Uses a separate memory card (the testcard folder next to killzone.ini), so testing never "
                                  "touches your profiles. Recommended with Start in.",
                                  cfg.testMemoryCard, "On (test card)", "Off (your memory card)");
                        row("Empty-card profile",
                            "Used only when Start in reaches Create Profile on an empty isolated test card. Override with "
                            "[Testing] DefaultProfile or KZ_DEFAULT_PROFILE.");
                        ImGui::TextUnformatted(cfg.defaultProfile.c_str());
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("Testing options are for development. Turn them off for normal play.");
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor();

        // Footer
        const float footerY = ImGui::GetWindowHeight() - 56.0f;
        ImGui::SetCursorPosY(footerY);
        ImGui::Separator();
        ImGui::Checkbox("Show this launcher at startup", &cfg.showLauncher);
        ImGui::SameLine(ImGui::GetWindowWidth() - 250);
        if (ImGui::Button("Quit", ImVec2(110, 32)))
            st.quit = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!st.disc.ok);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.20f, 0.14f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.28f, 0.20f, 1));
        if (ImGui::Button("PLAY", ImVec2(110, 32)))
            st.play = true;
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::End();
    }

    struct Session
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        bool ok = false;
    };

    Session openSession(bool hidden)
    {
        Session s;
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            return s;
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden ? SDL_WINDOW_HIDDEN : 0);
        s.window = SDL_CreateWindow("Killzone - Launcher", kWidth, kHeight, flags);
        if (!s.window)
            return s;
        s.renderer = SDL_CreateRenderer(s.window, hidden ? "software" : nullptr);
        if (!s.renderer)
            return s;
        SDL_SetRenderVSync(s.renderer, 1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        // System UI font; ImGui's built-in bitmap font is the fallback.
        if (SDL_GetPathInfo("C:/Windows/Fonts/segoeui.ttf", nullptr))
            ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f);
        applyStyle();
        ImGui_ImplSDL3_InitForSDLRenderer(s.window, s.renderer);
        ImGui_ImplSDLRenderer3_Init(s.renderer);
        s.ok = true;
        return s;
    }

    void closeSession(Session &s)
    {
        if (s.ok)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        if (s.renderer)
            SDL_DestroyRenderer(s.renderer);
        if (s.window)
            SDL_DestroyWindow(s.window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void renderFrame(LauncherState &st, Session &s)
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUi(st, s.window);
        ImGui::Render();
        SDL_SetRenderDrawColor(s.renderer, 27, 27, 28, 255);
        SDL_RenderClear(s.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s.renderer);
    }
}

bool kzRunLauncher(KzConfig &cfg)
{
    Session s = openSession(false);
    if (!s.ok)
    {
        SDL_Log("[launcher] could not open window: %s", SDL_GetError());
        closeSession(s);
        return !cfg.isoPath.empty(); // fall through to the game with saved settings
    }
    LauncherState st{cfg};
    st.resolutions = desktopResolutions();
    while (!st.play && !st.quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                st.quit = true;
        }
        renderFrame(st, s);
        SDL_RenderPresent(s.renderer);
    }
    if (!kzSaveConfig(kzConfigPath(), cfg))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Killzone",
                                 "Could not save killzone.ini. See killzone.log for details.", s.window);
        closeSession(s);
        return false;
    }
    closeSession(s);
    return st.play;
}

bool kzLauncherScreenshot(KzConfig &cfg, const char *pngPath)
{
    Session s = openSession(true);
    bool ok = false;
    if (s.ok)
    {
        LauncherState st{cfg};
        st.resolutions = desktopResolutions();
        for (int i = 0; i < 3; ++i) // let ImGui settle layout
            renderFrame(st, s);
        if (SDL_Surface *surf = SDL_RenderReadPixels(s.renderer, nullptr))
        {
            ok = SDL_SavePNG(surf, pngPath);
            SDL_DestroySurface(surf);
        }
    }
    closeSession(s);
    return ok;
}

int kzLauncherSelfTest()
{
    int failures = 0;
    const auto check = [&](bool condition, const char *label)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[selftest] FAIL: launcher %s\n", label);
            ++failures;
        }
    };

    KzConfig cfg;
    cfg.renderer = KzRenderer::Vulkan;
    cfg.vsync = false;
    cfg.upscale = 8;
    cfg.upscaler = KzgsUpscaler::FSR1;
    cfg.upscalerSharpening = 77;
    cfg.fsr3InputMode = KzgsFsr3InputMode::Experimental;
    cfg.aspect = KzAspect::Classic4x3;
    cfg.anisotropy = 0;
    cfg.mipmapMode = KzMipmapMode::PS2;
    cfg.fxaa = true;
    cfg.smaa = true;
    cfg.textureReplacement = true;
    cfg.bilinear = false;
    cfg.sharpScaling = false;
    cfg.sharpen = 0;
    cfg.noiseFilter = true;
    cfg.motionBlur = true;
    cfg.glow = true;
    cfg.lensBlur = true;
    cfg.renderDistance = 1.0f;
    cfg.lodScale = 4.0f;
    cfg.masterVolume = 37;
    cfg.startLevel = 7;

    applyValidatedGraphicsPreset(cfg);
    check(cfg.renderer == KzRenderer::D3D12 && cfg.vsync && cfg.upscale == 0 &&
              cfg.upscaler == KzgsUpscaler::Off && cfg.upscalerSharpening == 0 &&
              cfg.fsr3InputMode == KzgsFsr3InputMode::Strict &&
              cfg.aspect == KzAspect::Widescreen16x9,
          "validated preset output contract");
    check(cfg.anisotropy == 16 && cfg.mipmapMode == KzMipmapMode::HighestDetail &&
              !cfg.textureReplacement && cfg.bilinear,
          "validated preset texture controls");
    check(!cfg.fxaa && !cfg.smaa && cfg.sharpScaling && cfg.sharpen == 30,
          "validated preset presentation controls");
    check(!cfg.noiseFilter && !cfg.motionBlur && !cfg.glow && !cfg.lensBlur,
          "validated preset PS2 effects");
    check(cfg.renderDistance == 2.0f && cfg.lodScale == 1.0f,
          "validated preset detail bounds");
    check(cfg.masterVolume == 37 && cfg.startLevel == 7,
          "validated preset preserves unrelated settings");

    cfg.aspect = KzAspect::Classic4x3;
    cfg.renderDistance = 1.5f;
    applyFsr1QualityGraphicsPreset(cfg);
    check(cfg.renderer == KzRenderer::Vulkan && cfg.upscale == 3 &&
              cfg.upscaler == KzgsUpscaler::FSR1 && cfg.upscalerSharpening == 15 &&
              cfg.fsr3InputMode == KzgsFsr3InputMode::Strict,
          "FSR 1 quality preset output contract");
    check(cfg.anisotropy == 16 && cfg.mipmapMode == KzMipmapMode::PS2 && cfg.bilinear,
          "FSR 1 quality preset texture controls");
    check(!cfg.fxaa && !cfg.smaa && cfg.sharpScaling && cfg.sharpen == 0,
          "FSR 1 quality preset presentation controls");
    check(cfg.aspect == KzAspect::Classic4x3 && cfg.renderDistance == 1.5f &&
              cfg.masterVolume == 37 && cfg.startLevel == 7,
          "FSR 1 quality preset preserves unrelated settings");
    check(!fsr3InputModeControlEnabled(cfg), "FSR 3 input mode control is disabled for FSR 1");
    cfg.upscaler = KzgsUpscaler::FSR3;
    check(fsr3InputModeControlEnabled(cfg), "FSR 3 input mode control is enabled for Vulkan and FSR 3");
    check(providerSharpeningControlEnabled(cfg), "provider sharpening is enabled for FSR 3");
    cfg.renderer = KzRenderer::D3D12;
    check(!fsr3InputModeControlEnabled(cfg), "FSR 3 input mode control is disabled outside Vulkan");
    cfg.upscaler = KzgsUpscaler::XeSS;
    check(!providerSharpeningControlEnabled(cfg), "provider sharpening remains disabled for XeSS");

    check(externalFrameGenerationFpsLimit(60) == 60 && externalFrameGenerationFpsLimit(100) == 60 &&
              externalFrameGenerationFpsLimit(120) == 60 && externalFrameGenerationFpsLimit(144) == 72 &&
              externalFrameGenerationFpsLimit(165) == 82 && externalFrameGenerationFpsLimit(240) == 120,
          "external frame generation runs the game at half refresh, at least 60 fps");
    cfg.windowMode = KzWindowMode::Fullscreen;
    cfg.vsync = true;
    cfg.fpsLimit = 0;
    cfg.framePacing = false;
    cfg.renderer = KzRenderer::Vulkan;
    applyExternalFrameGenerationPreset(cfg, 144);
    check(cfg.windowMode == KzWindowMode::Borderless && !cfg.vsync && cfg.fpsLimit == 72 && !cfg.framePacing,
          "external frame generation preset display contract");
    check(cfg.renderer == KzRenderer::Vulkan && cfg.masterVolume == 37 && cfg.startLevel == 7,
          "external frame generation preset preserves unrelated settings");

    return failures;
}
