#define KZ_CONFIG_IMPLEMENTATION
#include "kz_config.h"
#include "kz_input.h"
#include "kz_host/core.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    constexpr int kConfigSchemaVersion = 3;

    std::string trim(std::string s)
    {
        const auto b = s.find_first_not_of(" \t\r");
        const auto e = s.find_last_not_of(" \t\r");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }

    template <typename E>
    struct EnumName
    {
        E value;
        const char *name;
    };

    constexpr EnumName<KzWindowMode> kWindowModes[] = {
        {KzWindowMode::Windowed, "windowed"}, {KzWindowMode::Borderless, "borderless"}, {KzWindowMode::Fullscreen, "fullscreen"}};
    constexpr EnumName<KzRenderer> kRenderers[] = {
        {KzRenderer::D3D11, "d3d11"}, {KzRenderer::D3D12, "d3d12"}, {KzRenderer::Vulkan, "vulkan"}};
    constexpr EnumName<KzgsFsr3InputMode> kFsr3InputModes[] = {
        {KzgsFsr3InputMode::Strict, "strict"}, {KzgsFsr3InputMode::Experimental, "experimental"}};
    constexpr EnumName<KzAspect> kAspects[] = {
        {KzAspect::Widescreen16x9, "16:9"},
        {KzAspect::Classic4x3, "4:3"},
        {KzAspect::Stretch, "stretch"},
        {KzAspect::UltrawideAuto, "auto"},
        {KzAspect::Ultrawide21x9, "21:9"},
        {KzAspect::Ultrawide32x9, "32:9"}};
    constexpr EnumName<KzMipmapMode> kMipmapModes[] = {
        {KzMipmapMode::PS2, "ps2"}, {KzMipmapMode::HighestDetail, "highest"}};
    enum class AimMode
    {
        Hold,
        Toggle,
    };
    constexpr EnumName<AimMode> kAimModes[] = {{AimMode::Hold, "Hold"}, {AimMode::Toggle, "Toggle"}};
    enum class SprintMode
    {
        Hold,
        Toggle,
    };
    constexpr EnumName<SprintMode> kSprintModes[] = {
        {SprintMode::Hold, "Hold"}, {SprintMode::Toggle, "Toggle"}};

    template <typename E, size_t N>
    const char *toName(const EnumName<E> (&table)[N], E v)
    {
        for (const auto &e : table)
            if (e.value == v)
                return e.name;
        return table[0].name;
    }

    template <typename E, size_t N>
    bool fromName(const EnumName<E> (&table)[N], const std::string &s, E &value)
    {
        for (const auto &e : table)
            if (_stricmp(e.name, s.c_str()) == 0)
            {
                value = e.value;
                return true;
            }
        return false;
    }

    bool parseFsr3InputMode(std::string_view text, KzgsFsr3InputMode &mode)
    {
        return fromName(kFsr3InputModes, std::string(text), mode);
    }

    std::optional<KzgsFsr3InputMode> fsr3InputModeEnvironmentOverride(
        const char *value, std::ostream &diagnostics)
    {
        if (!value)
            return std::nullopt;
        KzgsFsr3InputMode parsed;
        if (parseFsr3InputMode(value, parsed))
            return parsed;
        diagnostics << "[kz-config] error: KZ_FSR3_INPUT_MODE rejected value '" << value
                    << "': expected strict or experimental\n";
        return KzgsFsr3InputMode::Strict;
    }

    KzgsUpscalerOptionResult parseFsr3InputModeOption(
        std::string_view argument, const char *next, std::optional<KzgsFsr3InputMode> &override,
        bool &consumeNext, std::ostream &diagnostics)
    {
        consumeNext = false;
        constexpr std::string_view option = "--fsr3-input-mode";
        const bool recognized = argument == option || argument.starts_with("--fsr3-input-mode=");
        if (!recognized)
        {
            if (!argument.starts_with(option))
                return KzgsUpscalerOptionResult::NotHandled;
            override = KzgsFsr3InputMode::Strict;
            diagnostics << "[kz-config] error: unknown FSR 3 input mode option '" << argument << "'\n";
            return KzgsUpscalerOptionResult::Error;
        }

        std::string_view value;
        if (argument == option)
        {
            if (!next)
            {
                override = KzgsFsr3InputMode::Strict;
                diagnostics << "[kz-config] error: " << option << " requires a value\n";
                return KzgsUpscalerOptionResult::Error;
            }
            value = next;
            consumeNext = true;
        }
        else
        {
            value = argument.substr(option.size() + 1);
        }

        KzgsFsr3InputMode parsed;
        if (!parseFsr3InputMode(value, parsed))
        {
            override = KzgsFsr3InputMode::Strict;
            diagnostics << "[kz-config] error: " << option << " rejected value '" << value
                        << "': expected strict or experimental\n";
            return KzgsUpscalerOptionResult::Error;
        }
        override = parsed;
        return KzgsUpscalerOptionResult::Parsed;
    }

    std::optional<KzgsFsr3InputMode> s_fsr3InputModeCommandLineOverride;

    enum class DiagnosticLevel
    {
        Warning,
        Error,
    };

    struct ConfigDiagnostic
    {
        DiagnosticLevel level;
        size_t line = 0;
        std::string section;
        std::string key;
        std::string value;
        bool rejectedValue = false;
        std::string message;
    };

    struct RawSetting
    {
        std::string section;
        std::string key;
        std::string value;
        size_t line = 0;
    };

    struct ParsedIni
    {
        bool exists = false;
        std::vector<RawSetting> entries;
        std::map<std::string, size_t> latest;
        std::vector<ConfigDiagnostic> diagnostics;
    };

    struct ConfigLoadResult
    {
        KzConfig config;
        ParsedIni document;
        std::set<std::string> managedSettings;
        int schemaVersion = 0;
    };

    std::string settingId(const std::string &section, const std::string &key)
    {
        return section + '\x1f' + key;
    }

    bool sameName(const std::string &a, const char *b)
    {
        return _stricmp(a.c_str(), b) == 0;
    }

    void addDiagnostic(ParsedIni &ini, DiagnosticLevel level, size_t line, std::string section, std::string key,
                       std::string value, bool rejectedValue, std::string message)
    {
        ini.diagnostics.push_back(
            {level, line, std::move(section), std::move(key), std::move(value), rejectedValue, std::move(message)});
    }

    ParsedIni parseIni(const std::filesystem::path &path)
    {
        ParsedIni ini;
        std::ifstream in(path);
        if (!in)
            return ini;
        ini.exists = true;

        std::string section;
        std::string line;
        size_t lineNumber = 0;
        while (std::getline(in, line))
        {
            ++lineNumber;
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if (line.front() == '[')
            {
                if (line.back() != ']' || line.size() < 3)
                {
                    addDiagnostic(ini, DiagnosticLevel::Error, lineNumber, section, {}, line, true,
                                  "malformed section header");
                    continue;
                }
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                addDiagnostic(ini, DiagnosticLevel::Error, lineNumber, section, {}, line, true,
                              "expected Key=Value");
                continue;
            }

            RawSetting setting{section, trim(line.substr(0, eq)), trim(line.substr(eq + 1)), lineNumber};
            if (setting.key.empty())
            {
                addDiagnostic(ini, DiagnosticLevel::Error, lineNumber, section, {}, setting.value, true,
                              "setting name is empty");
                continue;
            }

            const size_t index = ini.entries.size();
            ini.entries.push_back(setting);
            if (sameName(section, "Bindings"))
                continue;

            const std::string id = settingId(setting.section, setting.key);
            const auto [it, inserted] = ini.latest.emplace(id, index);
            if (!inserted)
            {
                addDiagnostic(ini, DiagnosticLevel::Warning, lineNumber, setting.section, setting.key, setting.value,
                              false, "duplicate setting; the last value is used");
                it->second = index;
            }
        }
        return ini;
    }

    bool parseInteger(const std::string &text, int &value)
    {
        if (text.empty())
            return false;
        const char *begin = text.data();
        const char *end = begin + text.size();
        const auto result = std::from_chars(begin, end, value);
        return result.ec == std::errc() && result.ptr == end;
    }

    bool parseFloat(const std::string &text, float &value)
    {
        if (text.empty())
            return false;
        const char *begin = text.data();
        const char *end = begin + text.size();
        const auto result = std::from_chars(begin, end, value, std::chars_format::general);
        return result.ec == std::errc() && result.ptr == end && std::isfinite(value);
    }

    class ConfigReader
    {
    public:
        ConfigReader(ParsedIni &ini, std::set<std::string> &managedSettings)
            : m_ini(ini), m_managedSettings(managedSettings)
        {
        }

        const RawSetting *find(const char *section, const char *key)
        {
            const std::string id = settingId(section, key);
            m_managedSettings.insert(id);
            const auto it = m_ini.latest.find(id);
            return it == m_ini.latest.end() ? nullptr : &m_ini.entries[it->second];
        }

        const RawSetting *findWithAlias(const char *section, const char *key, const char *alias)
        {
            const RawSetting *setting = find(section, key);
            const RawSetting *aliasSetting = find(section, alias);
            if (setting && aliasSetting)
            {
                addDiagnostic(m_ini, DiagnosticLevel::Warning, aliasSetting->line, aliasSetting->section,
                              aliasSetting->key, aliasSetting->value, true,
                              std::string("legacy alias ignored because ") + key + " is present");
            }
            return setting ? setting : aliasSetting;
        }

        bool contains(const char *section, const char *key)
        {
            return find(section, key) != nullptr;
        }

        bool boolean(const char *section, const char *key, bool fallback)
        {
            const RawSetting *setting = find(section, key);
            if (!setting)
                return fallback;
            if (setting->value == "1" || sameName(setting->value, "true") || sameName(setting->value, "on"))
                return true;
            if (setting->value == "0" || sameName(setting->value, "false") || sameName(setting->value, "off"))
                return false;
            reject(*setting, "expected a boolean (0/1, true/false or on/off)");
            return fallback;
        }

        int integer(const char *section, const char *key, int fallback, int minimum, int maximum)
        {
            const RawSetting *setting = find(section, key);
            if (!setting)
                return fallback;
            int value = 0;
            if (!parseInteger(setting->value, value))
            {
                reject(*setting, "expected an integer");
                return fallback;
            }
            if (value < minimum || value > maximum)
            {
                addDiagnostic(m_ini, DiagnosticLevel::Warning, setting->line, setting->section, setting->key,
                              setting->value, false,
                              "out of range; clamped to " + std::to_string(std::clamp(value, minimum, maximum)));
                return std::clamp(value, minimum, maximum);
            }
            return value;
        }

        float number(const char *section, const char *key, float fallback, float minimum, float maximum)
        {
            return number(find(section, key), fallback, minimum, maximum);
        }

        float numberWithAlias(const char *section, const char *key, const char *alias, float fallback,
                              float minimum, float maximum)
        {
            return number(findWithAlias(section, key, alias), fallback, minimum, maximum);
        }

        template <typename E, size_t N>
        E enumeration(const char *section, const char *key, E fallback, const EnumName<E> (&table)[N])
        {
            const RawSetting *setting = find(section, key);
            if (!setting)
                return fallback;
            E value{};
            if (fromName(table, setting->value, value))
                return value;

            std::string expected = "expected one of: ";
            for (size_t i = 0; i < N; ++i)
            {
                if (i != 0)
                    expected += ", ";
                expected += table[i].name;
            }
            reject(*setting, std::move(expected));
            return fallback;
        }

        template <typename T>
        T validated(const char *section, const char *key, T fallback,
                    bool (*parse)(std::string_view, T &), const char *expected)
        {
            const RawSetting *setting = find(section, key);
            if (!setting)
                return fallback;
            T value{};
            if (parse(setting->value, value))
                return value;
            reject(*setting, expected);
            return fallback;
        }

        std::string text(const char *section, const char *key, std::string fallback)
        {
            const RawSetting *setting = find(section, key);
            return setting ? setting->value : std::move(fallback);
        }

        std::string validatedText(const char *section, const char *key, std::string fallback,
                                  bool (*isValid)(std::string_view), const char *expected)
        {
            const RawSetting *setting = find(section, key);
            if (!setting)
                return fallback;
            if (isValid(setting->value))
                return setting->value;
            reject(*setting, expected);
            return fallback;
        }

    private:
        float number(const RawSetting *setting, float fallback, float minimum, float maximum)
        {
            if (!setting)
                return fallback;
            float value = 0.0f;
            if (!parseFloat(setting->value, value))
            {
                reject(*setting, "expected a finite number");
                return fallback;
            }
            if (value < minimum || value > maximum)
            {
                std::ostringstream message;
                message.imbue(std::locale::classic());
                message << "out of range; clamped to " << std::clamp(value, minimum, maximum);
                addDiagnostic(m_ini, DiagnosticLevel::Warning, setting->line, setting->section, setting->key,
                              setting->value, false, message.str());
                return std::clamp(value, minimum, maximum);
            }
            return value;
        }

        void reject(const RawSetting &setting, std::string message)
        {
            addDiagnostic(m_ini, DiagnosticLevel::Error, setting.line, setting.section, setting.key, setting.value,
                          true, std::move(message));
        }

        ParsedIni &m_ini;
        std::set<std::string> &m_managedSettings;
    };

    ConfigLoadResult loadConfigFile(const std::filesystem::path &path)
    {
        ConfigLoadResult result;
        result.document = parseIni(path);
        ConfigReader reader(result.document, result.managedSettings);

        if (const RawSetting *version = reader.find("Config", "Version"))
        {
            if (!parseInteger(version->value, result.schemaVersion) || result.schemaVersion < 0)
            {
                addDiagnostic(result.document, DiagnosticLevel::Error, version->line, version->section, version->key,
                              version->value, true, "expected a non-negative schema version");
                result.schemaVersion = 0;
            }
            else if (result.schemaVersion > kConfigSchemaVersion)
            {
                addDiagnostic(result.document, DiagnosticLevel::Error, version->line, version->section, version->key,
                              version->value, true,
                              "unsupported schema version; this build supports up to " +
                                  std::to_string(kConfigSchemaVersion));
            }
            else if (result.schemaVersion < kConfigSchemaVersion)
            {
                addDiagnostic(result.document, DiagnosticLevel::Warning, version->line, version->section, version->key,
                              version->value, false,
                              "legacy schema will be migrated to version " + std::to_string(kConfigSchemaVersion) +
                                  " on the next save");
            }
        }
        else if (result.document.exists)
        {
            addDiagnostic(result.document, DiagnosticLevel::Warning, 0, "Config", "Version", {}, false,
                          "missing schema version; treating the file as legacy version 0");
        }

        KzConfig &c = result.config;
        c.windowMode = reader.enumeration("Display", "WindowMode", c.windowMode, kWindowModes);
        c.width = reader.integer("Display", "Width", c.width, 0, std::numeric_limits<int>::max());
        c.height = reader.integer("Display", "Height", c.height, 0, std::numeric_limits<int>::max());
        c.vsync = reader.boolean("Display", "VSync", c.vsync);
        c.fpsLimit = reader.integer("Display", "FpsLimit", c.fpsLimit, 0, std::numeric_limits<int>::max());
        c.showFps = reader.boolean("Display", "ShowFps", c.showFps);
        c.threadedGraphics = reader.integer("Display", "ThreadedGraphics", c.threadedGraphics, -1, 1);
        c.renderer = reader.enumeration("Graphics", "Renderer", c.renderer, kRenderers);
        c.upscale = reader.integer("Graphics", "Upscale", c.upscale, 0, 8);
        c.upscaler = reader.validated("Graphics", "Upscaler", c.upscaler, &kzgsParseUpscaler,
                                      "expected off, fsr1, fsr3, dlss or xess");
        c.upscalerSharpening = reader.validated("Graphics", "UpscalerSharpening", c.upscalerSharpening,
                                                &kzgsParseUpscalerSharpening,
                                                "expected an integer from 0 to 100");
        c.fsr3InputMode = reader.validated("Graphics", "FSR3InputMode", c.fsr3InputMode,
                                           &parseFsr3InputMode, "expected strict or experimental");
        c.aspect = reader.enumeration("Graphics", "Aspect", c.aspect, kAspects);
        c.anisotropy = reader.integer("Graphics", "Anisotropy", c.anisotropy, 0, 16);
        c.mipmapMode = reader.enumeration("Graphics", "Mipmapping", c.mipmapMode, kMipmapModes);
        c.fxaa = reader.boolean("Graphics", "FXAA", c.fxaa);
        c.smaa = reader.boolean("Graphics", "SMAA", c.smaa);
        c.smaaThreshold = reader.number("Graphics", "SmaaThreshold", c.smaaThreshold, 0.05f, 0.5f);
        c.textureReplacement = reader.boolean("Graphics", "TextureReplacement", c.textureReplacement);
        c.bilinear = reader.boolean("Graphics", "Bilinear", c.bilinear);
        c.sharpScaling = reader.boolean("Graphics", "SharpScaling", c.sharpScaling);
        c.sharpen = reader.integer("Graphics", "Sharpen", c.sharpen, 0, 100);
        c.noiseFilter = reader.boolean("Graphics", "NoiseFilter", c.noiseFilter);
        c.motionBlur = reader.boolean("Graphics", "MotionBlur", c.motionBlur);
        c.glow = reader.boolean("Graphics", "Glow", c.glow);
        c.lensBlur = reader.boolean("Graphics", "LensBlur", c.lensBlur);
        c.renderDistance = reader.number("Graphics", "RenderDistance", c.renderDistance, 1.0f, 2.0f);
        c.lodScale = reader.number("Graphics", "LodScale", c.lodScale, 1.0f, 1.0f);
        c.fov = reader.numberWithAlias("Graphics", "FieldOfView", "Fov", c.fov, 0.0f, 180.0f);
        c.weaponFov =
            reader.numberWithAlias("Graphics", "WeaponFieldOfView", "WeaponFov", c.weaponFov, 0.0f, 180.0f);
        c.mouseSensitivity = reader.number("Input", "MouseSensitivity", c.mouseSensitivity, 0.05f, 20.0f);
        c.invertY = reader.boolean("Input", "InvertY", c.invertY);
        c.rawMouse = reader.boolean("Input", "RawMouse", c.rawMouse);
        c.holdAim = reader.enumeration("Input", "AimMode", AimMode::Hold, kAimModes) == AimMode::Hold;
        c.toggleSprint =
            reader.enumeration("Input", "SprintMode", SprintMode::Hold, kSprintModes) == SprintMode::Toggle;
        c.stickDeadzone = reader.number("Input", "StickDeadzone", c.stickDeadzone, 0.0f, 0.9f);
        c.stickRange = reader.number("Input", "StickRange", c.stickRange, 1.0f, 1.5f);
        c.buttonPrompts = reader.boolean("Input", "ButtonPrompts", c.buttonPrompts);
        c.masterVolume = reader.integer("Audio", "MasterVolume", c.masterVolume, 0, 100);
        c.skipLongIntro = reader.boolean("Game", "SkipIntro", c.skipLongIntro);
        c.skipLogos = reader.boolean("Game", "SkipLogos", c.skipLogos);
        c.isoPath = reader.text("Paths", "ISO", c.isoPath);
        c.unlockAllLevels = reader.boolean("Testing", "UnlockAllLevels", c.unlockAllLevels);
        c.skipIntro = reader.boolean("Testing", "SkipIntro", c.skipIntro);
        c.startLevel = reader.integer("Testing", "StartLevel", c.startLevel, 0, 11);
        c.startDifficulty = reader.integer("Testing", "StartDifficulty", c.startDifficulty, 0, 2);
        c.testMemoryCard = reader.boolean("Testing", "TestMemoryCard", c.testMemoryCard);
        c.defaultProfile = reader.validatedText(
            "Testing", "DefaultProfile", c.defaultProfile, &kzIsValidProfileName,
            "expected 1-32 characters using letters, digits, space, hyphen, underscore or period");
        c.showLauncher = reader.boolean("Launcher", "Show", c.showLauncher);

        if (result.schemaVersion == 0)
        {
            if (!reader.contains("Graphics", "SharpScaling"))
                c.fxaa = false;
            if (!reader.contains("Graphics", "SmaaThreshold") && c.sharpen == 0)
                c.sharpen = 30;
        }

        for (const auto &[id, index] : result.document.latest)
        {
            if (result.managedSettings.count(id) != 0)
                continue;
            const RawSetting &setting = result.document.entries[index];
            addDiagnostic(result.document, DiagnosticLevel::Warning, setting.line, setting.section, setting.key,
                          setting.value, false, "unknown setting; it will be preserved when saving");
        }
        return result;
    }

    void emitDiagnostics(const std::filesystem::path &path, const ParsedIni &ini)
    {
        for (const ConfigDiagnostic &diagnostic : ini.diagnostics)
        {
            std::cerr << "[kz-config] "
                      << (diagnostic.level == DiagnosticLevel::Error ? "error: " : "warning: ")
                      << path.string();
            if (diagnostic.line != 0)
                std::cerr << ':' << diagnostic.line;
            if (!diagnostic.section.empty())
                std::cerr << " [" << diagnostic.section << ']';
            if (!diagnostic.key.empty())
                std::cerr << ' ' << diagnostic.key;
            if (diagnostic.rejectedValue)
                std::cerr << " rejected value '" << diagnostic.value << '\'';
            std::cerr << ": " << diagnostic.message << '\n';
        }
    }

    struct ExtraSection
    {
        std::string name;
        std::vector<RawSetting> settings;
        bool written = false;
    };

    bool saveConfigFile(const std::filesystem::path &path, const KzConfig &c, bool reportErrors)
    {
        const ConfigLoadResult existing = loadConfigFile(path);
        if (existing.schemaVersion > kConfigSchemaVersion)
        {
            if (reportErrors)
                std::cerr << "[kz-config] error: refusing to overwrite " << path.string() << " because schema version "
                          << existing.schemaVersion << " is newer than this build\n";
            return false;
        }

        std::vector<ExtraSection> extras;
        for (const RawSetting &setting : existing.document.entries)
        {
            if (sameName(setting.section, "Bindings") ||
                existing.managedSettings.count(settingId(setting.section, setting.key)) != 0)
                continue;
            auto it = std::find_if(extras.begin(), extras.end(),
                                   [&](const ExtraSection &section) { return section.name == setting.section; });
            if (it == extras.end())
            {
                extras.push_back({setting.section});
                it = std::prev(extras.end());
            }
            it->settings.push_back(setting);
        }

        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << "; Killzone PC settings. Edited by the launcher; safe to edit by hand.\n\n";
        auto writeExtras = [&](const char *section, bool writeHeader) {
            auto it = std::find_if(extras.begin(), extras.end(),
                                   [&](const ExtraSection &extra) { return extra.name == section; });
            if (it == extras.end())
                return;
            if (writeHeader)
                o << '[' << it->name << "]\n";
            for (const RawSetting &setting : it->settings)
                o << setting.key << '=' << setting.value << '\n';
            it->written = true;
        };

        writeExtras("", false);
        o << "[Config]\nVersion=" << kConfigSchemaVersion << '\n';
        writeExtras("Config", false);
        o << "\n[Display]\nWindowMode=" << toName(kWindowModes, c.windowMode) << "\nWidth=" << c.width
          << "\nHeight=" << c.height << "\nVSync=" << (c.vsync ? 1 : 0) << "\nFpsLimit=" << c.fpsLimit
          << "\nShowFps=" << (c.showFps ? 1 : 0) << "\nThreadedGraphics=" << c.threadedGraphics << '\n';
        writeExtras("Display", false);
        o << "\n[Graphics]\nRenderer=" << toName(kRenderers, c.renderer) << "\nUpscale=" << c.upscale
          << "\nUpscaler=" << kzgsUpscalerName(c.upscaler)
          << "\nUpscalerSharpening=" << c.upscalerSharpening
          << "\nFSR3InputMode=" << toName(kFsr3InputModes, c.fsr3InputMode)
          << "\nAspect=" << toName(kAspects, c.aspect) << "\nAnisotropy=" << c.anisotropy
          << "\nMipmapping=" << toName(kMipmapModes, c.mipmapMode)
          << "\nFXAA=" << (c.fxaa ? 1 : 0) << "\nSMAA=" << (c.smaa ? 1 : 0)
          << "\nSmaaThreshold=" << c.smaaThreshold
          << "\nTextureReplacement=" << (c.textureReplacement ? 1 : 0)
          << "\nBilinear=" << (c.bilinear ? 1 : 0) << "\nSharpScaling=" << (c.sharpScaling ? 1 : 0)
          << "\nSharpen=" << c.sharpen << "\nNoiseFilter=" << (c.noiseFilter ? 1 : 0)
          << "\nMotionBlur=" << (c.motionBlur ? 1 : 0) << "\nGlow=" << (c.glow ? 1 : 0)
          << "\nLensBlur=" << (c.lensBlur ? 1 : 0) << "\nRenderDistance=" << c.renderDistance
          << "\nLodScale=" << c.lodScale << "\nFieldOfView=" << c.fov
          << "\nWeaponFieldOfView=" << c.weaponFov << '\n';
        writeExtras("Graphics", false);
        o << "\n[Input]\nMouseSensitivity=" << c.mouseSensitivity << "\nInvertY=" << (c.invertY ? 1 : 0)
          << "\nRawMouse=" << (c.rawMouse ? 1 : 0) << "\nAimMode=" << (c.holdAim ? "Hold" : "Toggle")
          << "\nSprintMode=" << (c.toggleSprint ? "Toggle" : "Hold")
          << "\nStickDeadzone=" << c.stickDeadzone << "\nStickRange=" << c.stickRange
          << "\nButtonPrompts=" << (c.buttonPrompts ? 1 : 0) << '\n';
        writeExtras("Input", false);
        o << "\n[Audio]\nMasterVolume=" << c.masterVolume << '\n';
        writeExtras("Audio", false);
        o << "\n[Game]\nSkipIntro=" << (c.skipLongIntro ? 1 : 0)
          << "\nSkipLogos=" << (c.skipLogos ? 1 : 0) << '\n';
        writeExtras("Game", false);
        o << "\n[Paths]\nISO=" << c.isoPath << '\n';
        writeExtras("Paths", false);
        o << "\n[Testing]\nUnlockAllLevels=" << (c.unlockAllLevels ? 1 : 0)
          << "\nSkipIntro=" << (c.skipIntro ? 1 : 0) << "\nStartLevel=" << c.startLevel
          << "\nStartDifficulty=" << c.startDifficulty << "\nTestMemoryCard=" << (c.testMemoryCard ? 1 : 0)
          << "\nDefaultProfile=" << c.defaultProfile << '\n';
        writeExtras("Testing", false);
        o << "\n[Launcher]\nShow=" << (c.showLauncher ? 1 : 0) << '\n';
        writeExtras("Launcher", false);

        for (ExtraSection &section : extras)
            if (!section.written && !section.name.empty())
            {
                o << '\n';
                writeExtras(section.name.c_str(), true);
            }

        o << "\n[Bindings]\n; Key=Target. Targets: pad buttons (Cross Circle Square Triangle L1 R1 L2 R2 L3 R3 Start Select\n"
          << "; Up Down Left Right) or MoveForward MoveBack StrafeLeft StrafeRight LookUp LookDown LookLeft LookRight.\n"
          << "; Mouse1 left, Mouse2 right, Mouse3 middle, Mouse4/Mouse5 side, WheelUp/WheelDown; keys by SDL key name.\n"
          << "Version=4\n";
        for (const auto &[key, target] : kzInputBindingPairs(path))
            o << key << '=' << target << '\n';

        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            if (reportErrors)
                std::cerr << "[kz-config] error: could not open " << path.string() << " for writing\n";
            return false;
        }
        out << o.str();
        out.flush();
        if (!out)
        {
            if (reportErrors)
                std::cerr << "[kz-config] error: could not finish writing " << path.string() << '\n';
            return false;
        }
        return true;
    }

    bool sameConfig(const KzConfig &a, const KzConfig &b)
    {
        return a.windowMode == b.windowMode && a.width == b.width && a.height == b.height && a.vsync == b.vsync &&
               a.fpsLimit == b.fpsLimit && a.showFps == b.showFps &&
               a.threadedGraphics == b.threadedGraphics && a.renderer == b.renderer && a.upscale == b.upscale &&
               a.upscaler == b.upscaler && a.upscalerSharpening == b.upscalerSharpening &&
               a.fsr3InputMode == b.fsr3InputMode &&
               a.upscalerOverrides.provider == b.upscalerOverrides.provider &&
               a.upscalerOverrides.sharpening == b.upscalerOverrides.sharpening &&
               a.fsr3InputModeOverride == b.fsr3InputModeOverride &&
               a.aspect == b.aspect && a.anisotropy == b.anisotropy && a.mipmapMode == b.mipmapMode &&
               a.fxaa == b.fxaa && a.smaa == b.smaa &&
               a.smaaThreshold == b.smaaThreshold && a.textureReplacement == b.textureReplacement &&
               a.bilinear == b.bilinear && a.sharpScaling == b.sharpScaling && a.sharpen == b.sharpen &&
               a.noiseFilter == b.noiseFilter && a.motionBlur == b.motionBlur && a.glow == b.glow &&
               a.lensBlur == b.lensBlur && a.renderDistance == b.renderDistance && a.lodScale == b.lodScale &&
               a.fov == b.fov && a.weaponFov == b.weaponFov &&
               a.mouseSensitivity == b.mouseSensitivity && a.invertY == b.invertY && a.rawMouse == b.rawMouse &&
               a.holdAim == b.holdAim && a.toggleSprint == b.toggleSprint &&
               a.stickDeadzone == b.stickDeadzone && a.stickRange == b.stickRange &&
               a.buttonPrompts == b.buttonPrompts &&
               a.masterVolume == b.masterVolume && a.skipLongIntro == b.skipLongIntro &&
               a.skipLogos == b.skipLogos &&
               a.isoPath == b.isoPath &&
               a.unlockAllLevels == b.unlockAllLevels && a.skipIntro == b.skipIntro &&
               a.startLevel == b.startLevel && a.startDifficulty == b.startDifficulty &&
               a.testMemoryCard == b.testMemoryCard && a.defaultProfile == b.defaultProfile &&
               a.showLauncher == b.showLauncher;
    }

    bool writeText(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream out(path, std::ios::trunc);
        out << text;
        return static_cast<bool>(out);
    }

    std::string readText(const std::filesystem::path &path)
    {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    bool hasDiagnostic(const ConfigLoadResult &result, DiagnosticLevel level, const char *section, const char *key,
                       const char *value = nullptr)
    {
        return std::any_of(result.document.diagnostics.begin(), result.document.diagnostics.end(),
                           [&](const ConfigDiagnostic &diagnostic) {
                               return diagnostic.level == level && diagnostic.section == section &&
                                      diagnostic.key == key && (!value || diagnostic.value == value);
                           });
    }
}

bool kzIsValidProfileName(std::string_view name)
{
    if (name.empty() || name.size() > 32)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
               c == '-' || c == '_' || c == '.';
    });
}

std::filesystem::path kzConfigPath()
{
    if (const char *overridePath = std::getenv("KZ_CONFIG"); overridePath && *overridePath)
        return std::filesystem::absolute(overridePath).lexically_normal();
    return kz::host::executableDirectory() / "killzone.ini";
}

KzConfig kzLoadConfig(const std::filesystem::path &path)
{
    ConfigLoadResult result = loadConfigFile(path);
    emitDiagnostics(path, result.document);
    KzConfig &config = result.config;
    config.upscalerOverrides = kzgsUpscalerEnvironmentOverrides(
        std::getenv("KZ_UPSCALER"), std::getenv("KZ_UPSCALER_SHARPENING"), std::cerr);
    config.fsr3InputModeOverride =
        fsr3InputModeEnvironmentOverride(std::getenv("KZ_FSR3_INPUT_MODE"), std::cerr);
    if (s_fsr3InputModeCommandLineOverride)
        config.fsr3InputModeOverride = s_fsr3InputModeCommandLineOverride;
    if (const char *value = std::getenv("KZ_ASPECT"); value && *value && !fromName(kAspects, value, config.aspect))
        std::cerr << "[kz-config] error: KZ_ASPECT rejected value '" << value
                  << "': expected 16:9, 4:3, stretch, auto, 21:9 or 32:9\n";
    auto applyFovOverride = [&](const char *name, float &target) {
        const char *value = std::getenv(name);
        if (!value || !*value)
            return;
        float parsed = 0.0f;
        if (!parseFloat(value, parsed) || parsed < 0.0f || parsed > 180.0f)
            std::cerr << "[kz-config] error: " << name << " rejected value '" << value
                      << "': expected a number from 0 to 180\n";
        else
            target = parsed;
    };
    applyFovOverride("KZ_FOV", config.fov);
    applyFovOverride("KZ_WEAPON_FOV", config.weaponFov);
    return config;
}

bool kzSaveConfig(const std::filesystem::path &path, const KzConfig &c)
{
    return saveConfigFile(path, c, true);
}

bool kzRendererSupportsSmaa(KzRenderer renderer)
{
    return renderer == KzRenderer::D3D11;
}

KzgsUpscalerOptionResult kzParseGraphicsOption(
    std::string_view argument, const char *next, KzgsUpscalerOverrides &overrides,
    bool &consumeNext, std::ostream &diagnostics)
{
    const KzgsUpscalerOptionResult inputMode = parseFsr3InputModeOption(
        argument, next, s_fsr3InputModeCommandLineOverride, consumeNext, diagnostics);
    if (inputMode != KzgsUpscalerOptionResult::NotHandled)
        return inputMode;
    return kzgsParseUpscalerOption(argument, next, overrides, consumeNext, diagnostics);
}

int kzConfigSelfTest()
{
    int failures = 0;
    const std::optional<KzgsFsr3InputMode> processCommandLineMode =
        s_fsr3InputModeCommandLineOverride;
    s_fsr3InputModeCommandLineOverride.reset();
    auto check = [&](bool ok, const char *what) {
        if (!ok)
        {
            std::cerr << "[kz-config selftest] FAIL: " << what << '\n';
            ++failures;
        }
    };
    check(kzRendererSupportsSmaa(KzRenderer::D3D11) &&
              !kzRendererSupportsSmaa(KzRenderer::D3D12) &&
              !kzRendererSupportsSmaa(KzRenderer::Vulkan),
          "SMAA renderer capability");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("kz_config_selftest_" + std::to_string(nonce));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        std::cerr << "[kz-config selftest] FAIL: create fixture directory: " << error.message() << '\n';
        s_fsr3InputModeCommandLineOverride = processCommandLineMode;
        return 1;
    }

    const std::filesystem::path baselinePath = root / "baseline.ini";
    const std::string baseline =
        "[Config]\nVersion=3\n\n"
        "[Display]\nWindowMode=borderless\nWidth=0\nHeight=0\nVSync=1\nFpsLimit=0\n\n"
        "[Graphics]\nRenderer=d3d12\nUpscale=0\nAspect=16:9\nAnisotropy=16\nMipmapping=ps2\nFXAA=0\nSMAA=1\n"
        "SmaaThreshold=0.2\nTextureReplacement=1\nBilinear=1\nSharpScaling=1\nSharpen=30\n"
        "NoiseFilter=0\nMotionBlur=0\nGlow=0\nLensBlur=0\nRenderDistance=1\nLodScale=1\n\n"
        "[Input]\nMouseSensitivity=1\nInvertY=0\nRawMouse=1\nAimMode=Hold\nSprintMode=Hold\n"
        "StickDeadzone=0.15\nStickRange=1.33\n\n"
        "[Audio]\nMasterVolume=100\n\n"
        "[Game]\nSkipIntro=0\n\n"
        "[Paths]\nISO=X:\\BYOD\\Killzone.iso\n\n"
        "[Testing]\nUnlockAllLevels=0\nSkipIntro=0\nStartLevel=0\nStartDifficulty=0\nTestMemoryCard=0\n"
        "DefaultProfile=Shooterman\n\n"
        "[Launcher]\nShow=1\n\n"
        "[Bindings]\nVersion=4\nW=MoveForward\nW=MenuUp\n";
    check(writeText(baselinePath, baseline), "write baseline fixture");
    const ConfigLoadResult baselineLoad = loadConfigFile(baselinePath);
    KzConfig baselineExpected;
    baselineExpected.renderer = KzRenderer::D3D12;
    baselineExpected.isoPath = "X:\\BYOD\\Killzone.iso";
    check(baselineLoad.document.diagnostics.empty(), "baseline fixture diagnostics");
    check(sameConfig(baselineLoad.config, baselineExpected), "baseline fixture effective values");
    const auto baselineBindings = kzInputBindingPairs(baselinePath);
    check(saveConfigFile(baselinePath, baselineLoad.config, false), "save baseline fixture");
    const ConfigLoadResult baselineRoundTrip = loadConfigFile(baselinePath);
    check(sameConfig(baselineLoad.config, baselineRoundTrip.config), "baseline effective round-trip");
    check(baselineBindings == kzInputBindingPairs(baselinePath), "baseline bindings round-trip");
    check(readText(baselinePath).find("[Config]\nVersion=3") != std::string::npos,
          "baseline schema version round-trip");
    check(readText(baselinePath).find(
              "\nUpscaler=off\nUpscalerSharpening=0\nFSR3InputMode=strict\n") != std::string::npos,
          "default upscaler and FSR 3 input mode canonical serialization");
    KzConfig editedBaseline = baselineRoundTrip.config;
    editedBaseline.vsync = false;
    editedBaseline.sharpen = 45;
    editedBaseline.toggleSprint = true;
    editedBaseline.stickRange = 1.42f;
    editedBaseline.skipLongIntro = true;
    editedBaseline.showFps = true;
    editedBaseline.threadedGraphics = 0;
    editedBaseline.aspect = KzAspect::UltrawideAuto;
    editedBaseline.fov = 100.0f;
    editedBaseline.weaponFov = 85.0f;
    editedBaseline.buttonPrompts = false;
    editedBaseline.skipLogos = true;
    editedBaseline.defaultProfile = "Automation_1";
    check(saveConfigFile(baselinePath, editedBaseline, false), "save launcher-edited baseline");
    check(sameConfig(loadConfigFile(baselinePath).config, editedBaseline),
          "launcher edit preserves unrelated settings and path");
    check(baselineBindings == kzInputBindingPairs(baselinePath), "launcher edit preserves bindings");

    const ConfigLoadResult missing = loadConfigFile(root / "missing.ini");
    check(sameConfig(missing.config, KzConfig{}), "missing file defaults");
    check(missing.document.diagnostics.empty(), "missing file diagnostics");
    check(missing.config.upscaler == KzgsUpscaler::Off && missing.config.upscalerSharpening == 0 &&
              missing.config.fsr3InputMode == KzgsFsr3InputMode::Strict &&
              !missing.config.upscalerOverrides.provider && !missing.config.upscalerOverrides.sharpening &&
              !missing.config.fsr3InputModeOverride,
          "presentation upscaler defaults to off and FSR 3 inputs default to strict with no one-run overrides");

    struct UpscalerCase
    {
        const char *input;
        const char *canonical;
        KzgsUpscaler provider;
        int sharpening;
    };
    const UpscalerCase upscalerCases[] = {
        {"OFF", "off", KzgsUpscaler::Off, 0},
        {"FsR1", "fsr1", KzgsUpscaler::FSR1, 100},
        {"DlSs", "dlss", KzgsUpscaler::DLSS, 37},
        {"XESS", "xess", KzgsUpscaler::XeSS, 1},
        {"fSr3", "fsr3", KzgsUpscaler::FSR3, 15},
    };
    for (const UpscalerCase &test : upscalerCases)
    {
        const std::filesystem::path path = root / (std::string("upscaler-") + test.canonical + ".ini");
        check(writeText(path, std::string("[Config]\nVersion=3\n[Graphics]\nUpscale=4\nUpscaler=") +
                                  test.input + "\nUpscalerSharpening=" + std::to_string(test.sharpening) +
                                  "\nSharpen=35\n"),
              "write presentation upscaler fixture");
        const ConfigLoadResult loaded = loadConfigFile(path);
        check(loaded.document.diagnostics.empty() && loaded.config.upscale == 4 &&
                  loaded.config.upscaler == test.provider && loaded.config.upscalerSharpening == test.sharpening &&
                  loaded.config.sharpen == 35,
              "presentation provider parsing preserves integer upscale and CAS");
        check(saveConfigFile(path, loaded.config, false), "save presentation upscaler fixture");
        const std::string saved = readText(path);
        check(saved.find(std::string("\nUpscaler=") + test.canonical +
                         "\nUpscalerSharpening=" + std::to_string(test.sharpening) + "\n") != std::string::npos,
              "presentation provider canonical serialization");
        check(sameConfig(loadConfigFile(path).config, loaded.config), "presentation provider round-trip");
    }

    struct Fsr3InputModeCase
    {
        const char *input;
        const char *canonical;
        KzgsFsr3InputMode mode;
    };
    const Fsr3InputModeCase fsr3InputModeCases[] = {
        {"STRICT", "strict", KzgsFsr3InputMode::Strict},
        {"ExPeRiMeNtAl", "experimental", KzgsFsr3InputMode::Experimental},
    };
    for (const Fsr3InputModeCase &test : fsr3InputModeCases)
    {
        const std::filesystem::path path = root / (std::string("fsr3-input-") + test.canonical + ".ini");
        check(writeText(path, std::string("[Config]\nVersion=3\n[Graphics]\nUpscaler=fsr3\nFSR3InputMode=") +
                                  test.input + "\n"),
              "write FSR 3 input mode fixture");
        const ConfigLoadResult loaded = loadConfigFile(path);
        check(loaded.document.diagnostics.empty() && loaded.config.upscaler == KzgsUpscaler::FSR3 &&
                  loaded.config.fsr3InputMode == test.mode,
              "FSR 3 input mode parsing");
        check(saveConfigFile(path, loaded.config, false), "save FSR 3 input mode fixture");
        check(readText(path).find(std::string("\nFSR3InputMode=") + test.canonical + "\n") != std::string::npos,
              "FSR 3 input mode canonical serialization");
        check(sameConfig(loadConfigFile(path).config, loaded.config), "FSR 3 input mode round-trip");
    }

    const std::filesystem::path invalidUpscalerPath = root / "invalid-upscaler.ini";
    check(writeText(invalidUpscalerPath,
                    "[Config]\nVersion=3\n[Graphics]\nUpscaler=fsr2\nUpscalerSharpening=-1\n"
                    "FSR3InputMode=automatic\n"),
          "write invalid presentation fixture");
    const ConfigLoadResult invalidUpscaler = loadConfigFile(invalidUpscalerPath);
    check(invalidUpscaler.config.upscaler == KzgsUpscaler::Off && invalidUpscaler.config.upscalerSharpening == 0 &&
              invalidUpscaler.config.fsr3InputMode == KzgsFsr3InputMode::Strict &&
              hasDiagnostic(invalidUpscaler, DiagnosticLevel::Error, "Graphics", "Upscaler", "fsr2") &&
              hasDiagnostic(invalidUpscaler, DiagnosticLevel::Error, "Graphics", "UpscalerSharpening", "-1") &&
              hasDiagnostic(invalidUpscaler, DiagnosticLevel::Error, "Graphics", "FSR3InputMode", "automatic"),
          "invalid presentation INI values fail closed and are diagnosed");
    for (const char *value : {"101", "12junk", "", "999999999999999999999"})
    {
        check(writeText(invalidUpscalerPath, std::string("[Config]\nVersion=3\n[Graphics]\nUpscalerSharpening=") +
                                                 value + "\n"),
              "write invalid sharpening fixture");
        const ConfigLoadResult invalid = loadConfigFile(invalidUpscalerPath);
        check(invalid.config.upscalerSharpening == 0 &&
                  hasDiagnostic(invalid, DiagnosticLevel::Error, "Graphics", "UpscalerSharpening", value),
              "out-of-range or malformed sharpening is rejected");
    }
    for (const char *value : {"", "approximate", "experimental trailing"})
    {
        check(writeText(invalidUpscalerPath,
                        std::string("[Config]\nVersion=3\n[Graphics]\nFSR3InputMode=") + value + "\n"),
              "write invalid FSR 3 input mode fixture");
        const ConfigLoadResult invalid = loadConfigFile(invalidUpscalerPath);
        check(invalid.config.fsr3InputMode == KzgsFsr3InputMode::Strict &&
                  hasDiagnostic(invalid, DiagnosticLevel::Error, "Graphics", "FSR3InputMode", value),
              "invalid FSR 3 input mode fails closed");
    }

    const std::filesystem::path precedenceUpscalerPath = root / "precedence-upscaler.ini";
    check(writeText(precedenceUpscalerPath,
                    "[Config]\nVersion=3\n[Graphics]\nUpscale=5\nUpscaler=FSR1\nUpscalerSharpening=10\n"
                    "FSR3InputMode=experimental\nSharpen=40\nFutureFilter=quincunx\n"),
          "write presentation precedence fixture");
    KzConfig upscalerConfig = loadConfigFile(precedenceUpscalerPath).config;
    std::ostringstream overrideDiagnostics;
    upscalerConfig.upscalerOverrides = kzgsUpscalerEnvironmentOverrides("DLSS", "45", overrideDiagnostics);
    upscalerConfig.fsr3InputModeOverride =
        fsr3InputModeEnvironmentOverride("strict", overrideDiagnostics);
    check(overrideDiagnostics.str().empty() &&
              upscalerConfig.upscalerOverrides.provider.value_or(upscalerConfig.upscaler) == KzgsUpscaler::DLSS &&
              upscalerConfig.upscalerOverrides.sharpening.value_or(upscalerConfig.upscalerSharpening) == 45 &&
              upscalerConfig.fsr3InputModeOverride == KzgsFsr3InputMode::Strict,
          "environment overrides INI independently");
    KzgsUpscalerOverrides cliOverrides;
    bool consumeNext = false;
    check(kzgsParseUpscalerOption("--upscaler", "XeSS", cliOverrides, consumeNext, overrideDiagnostics) ==
                  KzgsUpscalerOptionResult::Parsed && consumeNext,
          "parse separate upscaler CLI argument");
    kzgsMergeUpscalerOverrides(upscalerConfig.upscalerOverrides, cliOverrides);
    check(upscalerConfig.upscalerOverrides.provider.value_or(upscalerConfig.upscaler) == KzgsUpscaler::XeSS &&
              upscalerConfig.upscalerOverrides.sharpening.value_or(upscalerConfig.upscalerSharpening) == 45,
          "CLI provider wins while environment sharpening remains");
    check(kzgsParseUpscalerOption("--upscaler-sharpening=0", nullptr, cliOverrides, consumeNext,
                                 overrideDiagnostics) == KzgsUpscalerOptionResult::Parsed && !consumeNext,
          "parse inline CLI sharpening including zero");
    kzgsMergeUpscalerOverrides(upscalerConfig.upscalerOverrides, cliOverrides);
    check(upscalerConfig.upscalerOverrides.provider.value_or(upscalerConfig.upscaler) == KzgsUpscaler::XeSS &&
              upscalerConfig.upscalerOverrides.sharpening.value_or(upscalerConfig.upscalerSharpening) == 0,
          "CLI overrides environment and INI per setting");
    check(parseFsr3InputModeOption("--fsr3-input-mode", "experimental", upscalerConfig.fsr3InputModeOverride,
                                  consumeNext, overrideDiagnostics) == KzgsUpscalerOptionResult::Parsed &&
              consumeNext && upscalerConfig.fsr3InputModeOverride == KzgsFsr3InputMode::Experimental,
          "CLI FSR 3 input mode overrides environment and INI");
    check(kzSaveConfig(precedenceUpscalerPath, upscalerConfig), "launcher save with one-run overrides active");
    const std::string precedenceUpscalerSaved = readText(precedenceUpscalerPath);
    check(precedenceUpscalerSaved.find(
              "\nUpscale=5\nUpscaler=fsr1\nUpscalerSharpening=10\nFSR3InputMode=experimental\n") !=
                  std::string::npos &&
              precedenceUpscalerSaved.find("\nSharpen=40\n") != std::string::npos &&
              precedenceUpscalerSaved.find("FutureFilter=quincunx") != std::string::npos,
          "launcher save ignores one-run overrides and retains unknown settings");
    const KzConfig afterOverrides = loadConfigFile(precedenceUpscalerPath).config;
    check(afterOverrides.upscaler == KzgsUpscaler::FSR1 && afterOverrides.upscalerSharpening == 10 &&
              afterOverrides.fsr3InputMode == KzgsFsr3InputMode::Experimental &&
              !afterOverrides.upscalerOverrides.provider && !afterOverrides.upscalerOverrides.sharpening &&
              !afterOverrides.fsr3InputModeOverride,
          "one-run overrides do not survive a fresh INI load");

    const auto savedEnvironment = [](const char *name) -> std::optional<std::string> {
        const char *value = std::getenv(name);
        return value ? std::optional<std::string>(value) : std::nullopt;
    };
    const auto oldProvider = savedEnvironment("KZ_UPSCALER");
    const auto oldSharpening = savedEnvironment("KZ_UPSCALER_SHARPENING");
    const auto oldFsr3InputMode = savedEnvironment("KZ_FSR3_INPUT_MODE");
    kz::host::setEnvironment("KZ_UPSCALER", "dlss");
    kz::host::setEnvironment("KZ_UPSCALER_SHARPENING", "60");
    kz::host::setEnvironment("KZ_FSR3_INPUT_MODE", "experimental");
    const std::filesystem::path environmentPath = root / "upscaler-fsr1.ini";
    KzConfig actualEnvironment = kzLoadConfig(environmentPath);
    check(actualEnvironment.upscaler == KzgsUpscaler::FSR1 && actualEnvironment.upscalerSharpening == 100 &&
              actualEnvironment.fsr3InputMode == KzgsFsr3InputMode::Strict &&
              actualEnvironment.upscalerOverrides.provider == KzgsUpscaler::DLSS &&
              actualEnvironment.upscalerOverrides.sharpening == 60 &&
              actualEnvironment.fsr3InputModeOverride == KzgsFsr3InputMode::Experimental,
          "config loader reads real environment into one-run overrides");
    kzgsMergeUpscalerOverrides(actualEnvironment.upscalerOverrides, cliOverrides);
    check(kzSaveConfig(environmentPath, actualEnvironment), "launcher save with real environment and CLI overrides");
    const KzConfig environmentSaved = loadConfigFile(environmentPath).config;
    check(environmentSaved.upscaler == KzgsUpscaler::FSR1 && environmentSaved.upscalerSharpening == 100 &&
              environmentSaved.fsr3InputMode == KzgsFsr3InputMode::Strict,
          "real environment and CLI overrides are not persisted");
    kz::host::setEnvironment("KZ_UPSCALER", oldProvider.value_or(""));
    kz::host::setEnvironment("KZ_UPSCALER_SHARPENING", oldSharpening.value_or(""));
    kz::host::setEnvironment("KZ_FSR3_INPUT_MODE", oldFsr3InputMode.value_or(""));

    for (const char *value : {"", "fsr2", "fsr1 trailing"})
    {
        std::ostringstream errors;
        const KzgsUpscalerOverrides invalid = kzgsUpscalerEnvironmentOverrides(value, nullptr, errors);
        check(!invalid.provider && errors.str().find("KZ_UPSCALER rejected value") != std::string::npos,
              "invalid environment provider is diagnosed and ignored");
    }
    for (const char *value : {"", "-1", "101", "3x", "999999999999999999999"})
    {
        std::ostringstream errors;
        const KzgsUpscalerOverrides invalid = kzgsUpscalerEnvironmentOverrides(nullptr, value, errors);
        check(!invalid.sharpening &&
                  errors.str().find("KZ_UPSCALER_SHARPENING rejected value") != std::string::npos,
              "invalid environment sharpening is diagnosed and ignored");
    }
    for (const char *value : {"", "approximate", "experimental trailing"})
    {
        std::ostringstream errors;
        const std::optional<KzgsFsr3InputMode> invalid =
            fsr3InputModeEnvironmentOverride(value, errors);
        check(invalid == KzgsFsr3InputMode::Strict &&
                  errors.str().find("KZ_FSR3_INPUT_MODE rejected value") != std::string::npos,
              "invalid environment FSR 3 input mode fails closed");
    }
    struct InvalidUpscalerOption
    {
        const char *argument;
        const char *next;
    };
    const InvalidUpscalerOption invalidOptions[] = {
        {"--upscaler", nullptr},
        {"--upscaler=fsr2", nullptr},
        {"--upscaler-sharpening", nullptr},
        {"--upscaler-sharpening=-1", nullptr},
        {"--upscaler-sharpening=101", nullptr},
        {"--upscaler-sharpening=1x", nullptr},
        {"--upscaler-unknown", nullptr},
    };
    for (const InvalidUpscalerOption &test : invalidOptions)
    {
        std::ostringstream errors;
        KzgsUpscalerOverrides invalid;
        check(kzgsParseUpscalerOption(test.argument, test.next, invalid, consumeNext, errors) ==
                      KzgsUpscalerOptionResult::Error &&
                  !invalid.provider && !invalid.sharpening &&
                  errors.str().find("[kz-config] error:") != std::string::npos,
              "invalid or incomplete upscaler CLI option is rejected and diagnosed");
    }
    const InvalidUpscalerOption invalidInputModeOptions[] = {
        {"--fsr3-input-mode", nullptr},
        {"--fsr3-input-mode=approximate", nullptr},
        {"--fsr3-input-mode-unknown", nullptr},
    };
    for (const InvalidUpscalerOption &test : invalidInputModeOptions)
    {
        std::ostringstream errors;
        std::optional<KzgsFsr3InputMode> invalid = KzgsFsr3InputMode::Experimental;
        check(parseFsr3InputModeOption(test.argument, test.next, invalid, consumeNext, errors) ==
                      KzgsUpscalerOptionResult::Error &&
                  invalid == KzgsFsr3InputMode::Strict &&
                  errors.str().find("[kz-config] error:") != std::string::npos,
              "invalid or incomplete FSR 3 input mode CLI option fails closed");
    }

    const std::optional<KzgsFsr3InputMode> previousCommandLineMode =
        s_fsr3InputModeCommandLineOverride;
    s_fsr3InputModeCommandLineOverride.reset();
    KzgsUpscalerOverrides combinedOverrides;
    check(kzParseGraphicsOption("--fsr3-input-mode=experimental", nullptr, combinedOverrides,
                                consumeNext, overrideDiagnostics) == KzgsUpscalerOptionResult::Parsed &&
              !consumeNext && s_fsr3InputModeCommandLineOverride == KzgsFsr3InputMode::Experimental,
          "combined graphics CLI parser accepts inline FSR 3 input mode");
    const KzConfig commandLineMapped = kzLoadConfig(baselinePath);
    check(commandLineMapped.fsr3InputMode == KzgsFsr3InputMode::Strict &&
              commandLineMapped.fsr3InputModeOverride == KzgsFsr3InputMode::Experimental,
          "command-line FSR 3 input mode maps to a one-run config override");
    s_fsr3InputModeCommandLineOverride = previousCommandLineMode;

    const std::filesystem::path canonicalFovPath = root / "canonical-fov.ini";
    check(writeText(canonicalFovPath,
                    "[Config]\nVersion=3\n[Graphics]\nFieldOfView=105.5\nWeaponFieldOfView=80.5\n"),
          "write canonical FOV fixture");
    const ConfigLoadResult canonicalFov = loadConfigFile(canonicalFovPath);
    check(canonicalFov.config.fov == 105.5f && canonicalFov.config.weaponFov == 80.5f,
          "canonical FOV keys");
    check(canonicalFov.document.diagnostics.empty(), "canonical FOV diagnostics");

    const std::filesystem::path aliasFovPath = root / "alias-fov.ini";
    check(writeText(aliasFovPath, "[Config]\nVersion=3\n[Graphics]\nFov=101.5\nWeaponFov=84.5\n"),
          "write alias FOV fixture");
    const ConfigLoadResult aliasFov = loadConfigFile(aliasFovPath);
    check(aliasFov.config.fov == 101.5f && aliasFov.config.weaponFov == 84.5f, "FOV migration aliases");
    check(aliasFov.document.diagnostics.empty(), "FOV migration alias diagnostics");
    check(saveConfigFile(aliasFovPath, aliasFov.config, false), "save FOV alias fixture");
    const std::string aliasFovSaved = readText(aliasFovPath);
    check(aliasFovSaved.find("FieldOfView=101.5") != std::string::npos &&
              aliasFovSaved.find("WeaponFieldOfView=84.5") != std::string::npos &&
              aliasFovSaved.find("\nFov=") == std::string::npos &&
              aliasFovSaved.find("\nWeaponFov=") == std::string::npos,
          "FOV aliases migrate to canonical keys");
    check(sameConfig(loadConfigFile(aliasFovPath).config, aliasFov.config), "FOV alias migration round-trip");

    const std::filesystem::path precedenceFovPath = root / "precedence-fov.ini";
    check(writeText(precedenceFovPath,
                    "[Config]\nVersion=3\n[Graphics]\n"
                    "FieldOfView=112\nFov=90\nWeaponFieldOfView=88\nWeaponFov=70\n"),
          "write FOV precedence fixture");
    const ConfigLoadResult precedenceFov = loadConfigFile(precedenceFovPath);
    check(precedenceFov.config.fov == 112.0f && precedenceFov.config.weaponFov == 88.0f,
          "canonical FOV key precedence");
    check(hasDiagnostic(precedenceFov, DiagnosticLevel::Warning, "Graphics", "Fov", "90") &&
              hasDiagnostic(precedenceFov, DiagnosticLevel::Warning, "Graphics", "WeaponFov", "70"),
          "ignored FOV alias diagnostics");
    check(saveConfigFile(precedenceFovPath, precedenceFov.config, false), "save FOV precedence fixture");
    const std::string precedenceFovSaved = readText(precedenceFovPath);
    check(precedenceFovSaved.find("\nFov=") == std::string::npos &&
              precedenceFovSaved.find("\nWeaponFov=") == std::string::npos,
          "ignored FOV aliases removed on save");

    const std::filesystem::path partialPath = root / "partial.ini";
    check(writeText(partialPath, "[Config]\nVersion=3\n[Display]\nVSync=0\n"), "write partial fixture");
    const ConfigLoadResult partial = loadConfigFile(partialPath);
    check(!partial.config.vsync && partial.config.width == 0 && partial.config.renderer == KzRenderer::D3D11,
          "partial file defaults");
    check(partial.document.diagnostics.empty(), "partial file diagnostics");

    const std::filesystem::path versionOnePath = root / "version-one.ini";
    check(writeText(versionOnePath, "[Config]\nVersion=1\n[Graphics]\nAnisotropy=8\n"),
          "write version-one fixture");
    const ConfigLoadResult versionOne = loadConfigFile(versionOnePath);
    check(versionOne.config.anisotropy == 8 && versionOne.config.mipmapMode == KzMipmapMode::PS2,
          "version-one mipmapping default");
    check(hasDiagnostic(versionOne, DiagnosticLevel::Warning, "Config", "Version", "1"),
          "version-one migration diagnostic");
    check(saveConfigFile(versionOnePath, versionOne.config, false), "save version-one fixture");
    check(readText(versionOnePath).find("[Config]\nVersion=3") != std::string::npos,
          "version-one schema migration");

    const std::filesystem::path legacyPath = root / "legacy.ini";
    check(writeText(legacyPath, "[Graphics]\nFXAA=1\nSharpen=0\n"), "write legacy fixture");
    const ConfigLoadResult legacy = loadConfigFile(legacyPath);
    check(!legacy.config.fxaa && legacy.config.sharpen == 30, "legacy deterministic migration");
    check(hasDiagnostic(legacy, DiagnosticLevel::Warning, "Config", "Version"), "legacy migration diagnostic");
    check(saveConfigFile(legacyPath, legacy.config, false), "save migrated fixture");
    check(readText(legacyPath).find("[Config]\nVersion=3") != std::string::npos, "legacy schema migration");

    const std::filesystem::path unknownPath = root / "unknown.ini";
    check(writeText(unknownPath,
                    "[Config]\nVersion=3\n[Graphics]\nFutureFilter=quincunx\n[Plugin]\nMode=custom\n"),
          "write unknown-key fixture");
    const ConfigLoadResult unknown = loadConfigFile(unknownPath);
    check(hasDiagnostic(unknown, DiagnosticLevel::Warning, "Graphics", "FutureFilter", "quincunx"),
          "unknown-key diagnostic");
    check(saveConfigFile(unknownPath, unknown.config, false), "save unknown-key fixture");
    const std::string unknownSaved = readText(unknownPath);
    check(unknownSaved.find("FutureFilter=quincunx") != std::string::npos &&
              unknownSaved.find("[Plugin]\nMode=custom") != std::string::npos,
          "unknown settings preserved");

    const std::filesystem::path duplicatePath = root / "duplicate.ini";
    check(writeText(duplicatePath,
                    "[Config]\nVersion=3\n[Display]\nFpsLimit=60\nFpsLimit=144\n"),
          "write duplicate-key fixture");
    const ConfigLoadResult duplicate = loadConfigFile(duplicatePath);
    check(duplicate.config.fpsLimit == 144, "duplicate last value");
    check(hasDiagnostic(duplicate, DiagnosticLevel::Warning, "Display", "FpsLimit", "144"),
          "duplicate-key diagnostic");

    const std::filesystem::path renderDistancePath = root / "render-distance.ini";
    check(writeText(renderDistancePath, "[Config]\nVersion=3\n[Graphics]\nRenderDistance=4\n"),
          "write render-distance fixture");
    const ConfigLoadResult renderDistance = loadConfigFile(renderDistancePath);
    check(renderDistance.config.renderDistance == 2.0f, "render-distance safety clamp");
    check(hasDiagnostic(renderDistance, DiagnosticLevel::Warning, "Graphics", "RenderDistance", "4"),
          "render-distance clamp diagnostic");

    const std::filesystem::path malformedPath = root / "malformed.ini";
    check(writeText(malformedPath,
                    "[Config]\nVersion=3\n"
                    "[Display]\nWindowMode=hover\nWidth=wide\nVSync=maybe\n"
                    "[Graphics]\nRenderer=metal\nRenderDistance=near\nUpscale=99\n"
                    "[Testing]\nDefaultProfile=bad/name\n"),
          "write malformed-value fixture");
    const ConfigLoadResult malformed = loadConfigFile(malformedPath);
    check(malformed.config.windowMode == KzWindowMode::Borderless && malformed.config.width == 0 &&
              malformed.config.vsync && malformed.config.renderer == KzRenderer::D3D11 &&
              malformed.config.renderDistance == 1.0f &&
              malformed.config.defaultProfile == KzDefaultProfileName,
          "malformed values rejected");
    check(malformed.config.upscale == 8, "range clamp preserved");
    check(hasDiagnostic(malformed, DiagnosticLevel::Error, "Display", "WindowMode", "hover") &&
              hasDiagnostic(malformed, DiagnosticLevel::Error, "Display", "Width", "wide") &&
              hasDiagnostic(malformed, DiagnosticLevel::Error, "Display", "VSync", "maybe") &&
              hasDiagnostic(malformed, DiagnosticLevel::Error, "Graphics", "Renderer", "metal") &&
              hasDiagnostic(malformed, DiagnosticLevel::Error, "Graphics", "RenderDistance", "near") &&
              hasDiagnostic(malformed, DiagnosticLevel::Error, "Testing", "DefaultProfile", "bad/name"),
          "typed malformed-value diagnostics");
    check(hasDiagnostic(malformed, DiagnosticLevel::Warning, "Graphics", "Upscale", "99"),
          "range diagnostic");

    const std::filesystem::path futurePath = root / "future.ini";
    const std::string future = "[Config]\nVersion=4\n[Paths]\nISO=X:\\BYOD\\Future.iso\n";
    check(writeText(futurePath, future), "write future-version fixture");
    const ConfigLoadResult futureLoad = loadConfigFile(futurePath);
    check(hasDiagnostic(futureLoad, DiagnosticLevel::Error, "Config", "Version", "4"),
          "future-version diagnostic");
    check(!saveConfigFile(futurePath, futureLoad.config, false), "future-version save refusal");
    check(readText(futurePath) == future, "future-version file unchanged");

    std::filesystem::remove_all(root, error);
    check(!error, "remove fixture directory");
    s_fsr3InputModeCommandLineOverride = processCommandLineMode;
    return failures;
}

KzConfig &kzConfig()
{
    static KzConfig cfg;
    return cfg;
}
