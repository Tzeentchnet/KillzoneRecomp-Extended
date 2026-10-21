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
    constexpr int kConfigSchemaVersion = 1;

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
    constexpr EnumName<KzAspect> kAspects[] = {
        {KzAspect::Widescreen16x9, "16:9"}, {KzAspect::Classic4x3, "4:3"}, {KzAspect::Stretch, "stretch"}};
    enum class AimMode
    {
        Hold,
        Toggle,
    };
    constexpr EnumName<AimMode> kAimModes[] = {{AimMode::Hold, "Hold"}, {AimMode::Toggle, "Toggle"}};

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
            const RawSetting *setting = find(section, key);
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
        c.renderer = reader.enumeration("Graphics", "Renderer", c.renderer, kRenderers);
        c.upscale = reader.integer("Graphics", "Upscale", c.upscale, 0, 8);
        c.aspect = reader.enumeration("Graphics", "Aspect", c.aspect, kAspects);
        c.anisotropy = reader.integer("Graphics", "Anisotropy", c.anisotropy, 0, 16);
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
        c.renderDistance = reader.number("Graphics", "RenderDistance", c.renderDistance, 1.0f, 16.0f);
        c.lodScale = reader.number("Graphics", "LodScale", c.lodScale, 0.25f, 16.0f);
        c.mouseSensitivity = reader.number("Input", "MouseSensitivity", c.mouseSensitivity, 0.05f, 20.0f);
        c.invertY = reader.boolean("Input", "InvertY", c.invertY);
        c.rawMouse = reader.boolean("Input", "RawMouse", c.rawMouse);
        c.holdAim = reader.enumeration("Input", "AimMode", AimMode::Hold, kAimModes) == AimMode::Hold;
        c.stickDeadzone = reader.number("Input", "StickDeadzone", c.stickDeadzone, 0.0f, 0.9f);
        c.masterVolume = reader.integer("Audio", "MasterVolume", c.masterVolume, 0, 100);
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
          << "\nHeight=" << c.height << "\nVSync=" << (c.vsync ? 1 : 0) << "\nFpsLimit=" << c.fpsLimit << '\n';
        writeExtras("Display", false);
        o << "\n[Graphics]\nRenderer=" << toName(kRenderers, c.renderer) << "\nUpscale=" << c.upscale
          << "\nAspect=" << toName(kAspects, c.aspect) << "\nAnisotropy=" << c.anisotropy
          << "\nFXAA=" << (c.fxaa ? 1 : 0) << "\nSMAA=" << (c.smaa ? 1 : 0)
          << "\nSmaaThreshold=" << c.smaaThreshold
          << "\nTextureReplacement=" << (c.textureReplacement ? 1 : 0)
          << "\nBilinear=" << (c.bilinear ? 1 : 0) << "\nSharpScaling=" << (c.sharpScaling ? 1 : 0)
          << "\nSharpen=" << c.sharpen << "\nNoiseFilter=" << (c.noiseFilter ? 1 : 0)
          << "\nMotionBlur=" << (c.motionBlur ? 1 : 0) << "\nGlow=" << (c.glow ? 1 : 0)
          << "\nLensBlur=" << (c.lensBlur ? 1 : 0) << "\nRenderDistance=" << c.renderDistance
          << "\nLodScale=" << c.lodScale << '\n';
        writeExtras("Graphics", false);
        o << "\n[Input]\nMouseSensitivity=" << c.mouseSensitivity << "\nInvertY=" << (c.invertY ? 1 : 0)
          << "\nRawMouse=" << (c.rawMouse ? 1 : 0) << "\nAimMode=" << (c.holdAim ? "Hold" : "Toggle")
          << "\nStickDeadzone=" << c.stickDeadzone << '\n';
        writeExtras("Input", false);
        o << "\n[Audio]\nMasterVolume=" << c.masterVolume << '\n';
        writeExtras("Audio", false);
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
          << "Version=3\n";
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
               a.fpsLimit == b.fpsLimit && a.renderer == b.renderer && a.upscale == b.upscale &&
               a.aspect == b.aspect && a.anisotropy == b.anisotropy && a.fxaa == b.fxaa && a.smaa == b.smaa &&
               a.smaaThreshold == b.smaaThreshold && a.textureReplacement == b.textureReplacement &&
               a.bilinear == b.bilinear && a.sharpScaling == b.sharpScaling && a.sharpen == b.sharpen &&
               a.noiseFilter == b.noiseFilter && a.motionBlur == b.motionBlur && a.glow == b.glow &&
               a.lensBlur == b.lensBlur && a.renderDistance == b.renderDistance && a.lodScale == b.lodScale &&
               a.mouseSensitivity == b.mouseSensitivity && a.invertY == b.invertY && a.rawMouse == b.rawMouse &&
               a.holdAim == b.holdAim && a.stickDeadzone == b.stickDeadzone &&
               a.masterVolume == b.masterVolume && a.isoPath == b.isoPath &&
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
    return result.config;
}

bool kzSaveConfig(const std::filesystem::path &path, const KzConfig &c)
{
    return saveConfigFile(path, c, true);
}

bool kzRendererSupportsSmaa(KzRenderer renderer)
{
    return renderer == KzRenderer::D3D11;
}

int kzConfigSelfTest()
{
    int failures = 0;
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
        return 1;
    }

    const std::filesystem::path baselinePath = root / "baseline.ini";
    const std::string baseline =
        "[Config]\nVersion=1\n\n"
        "[Display]\nWindowMode=borderless\nWidth=0\nHeight=0\nVSync=0\nFpsLimit=0\n\n"
        "[Graphics]\nRenderer=d3d12\nUpscale=0\nAspect=16:9\nAnisotropy=16\nFXAA=0\nSMAA=1\n"
        "SmaaThreshold=0.2\nTextureReplacement=1\nBilinear=1\nSharpScaling=1\nSharpen=30\n"
        "NoiseFilter=0\nMotionBlur=0\nGlow=0\nLensBlur=0\nRenderDistance=1\nLodScale=1\n\n"
        "[Input]\nMouseSensitivity=1\nInvertY=0\nRawMouse=1\nAimMode=Hold\nStickDeadzone=0.15\n\n"
        "[Audio]\nMasterVolume=100\n\n"
        "[Paths]\nISO=X:\\BYOD\\Killzone.iso\n\n"
        "[Testing]\nUnlockAllLevels=0\nSkipIntro=0\nStartLevel=0\nStartDifficulty=0\nTestMemoryCard=0\n"
        "DefaultProfile=Shooterman\n\n"
        "[Launcher]\nShow=1\n\n"
        "[Bindings]\nVersion=3\nW=MoveForward\nW=MenuUp\n";
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
    check(readText(baselinePath).find("[Config]\nVersion=1") != std::string::npos,
          "baseline schema version round-trip");
    KzConfig editedBaseline = baselineRoundTrip.config;
    editedBaseline.vsync = true;
    editedBaseline.sharpen = 45;
    editedBaseline.defaultProfile = "Automation_1";
    check(saveConfigFile(baselinePath, editedBaseline, false), "save launcher-edited baseline");
    check(sameConfig(loadConfigFile(baselinePath).config, editedBaseline),
          "launcher edit preserves unrelated settings and path");
    check(baselineBindings == kzInputBindingPairs(baselinePath), "launcher edit preserves bindings");

    const ConfigLoadResult missing = loadConfigFile(root / "missing.ini");
    check(sameConfig(missing.config, KzConfig{}), "missing file defaults");
    check(missing.document.diagnostics.empty(), "missing file diagnostics");

    const std::filesystem::path partialPath = root / "partial.ini";
    check(writeText(partialPath, "[Config]\nVersion=1\n[Display]\nVSync=1\n"), "write partial fixture");
    const ConfigLoadResult partial = loadConfigFile(partialPath);
    check(partial.config.vsync && partial.config.width == 0 && partial.config.renderer == KzRenderer::D3D11,
          "partial file defaults");
    check(partial.document.diagnostics.empty(), "partial file diagnostics");

    const std::filesystem::path legacyPath = root / "legacy.ini";
    check(writeText(legacyPath, "[Graphics]\nFXAA=1\nSharpen=0\n"), "write legacy fixture");
    const ConfigLoadResult legacy = loadConfigFile(legacyPath);
    check(!legacy.config.fxaa && legacy.config.sharpen == 30, "legacy deterministic migration");
    check(hasDiagnostic(legacy, DiagnosticLevel::Warning, "Config", "Version"), "legacy migration diagnostic");
    check(saveConfigFile(legacyPath, legacy.config, false), "save migrated fixture");
    check(readText(legacyPath).find("[Config]\nVersion=1") != std::string::npos, "legacy schema migration");

    const std::filesystem::path unknownPath = root / "unknown.ini";
    check(writeText(unknownPath,
                    "[Config]\nVersion=1\n[Graphics]\nFutureFilter=quincunx\n[Plugin]\nMode=custom\n"),
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
                    "[Config]\nVersion=1\n[Display]\nFpsLimit=60\nFpsLimit=144\n"),
          "write duplicate-key fixture");
    const ConfigLoadResult duplicate = loadConfigFile(duplicatePath);
    check(duplicate.config.fpsLimit == 144, "duplicate last value");
    check(hasDiagnostic(duplicate, DiagnosticLevel::Warning, "Display", "FpsLimit", "144"),
          "duplicate-key diagnostic");

    const std::filesystem::path malformedPath = root / "malformed.ini";
    check(writeText(malformedPath,
                    "[Config]\nVersion=1\n"
                    "[Display]\nWindowMode=hover\nWidth=wide\nVSync=maybe\n"
                    "[Graphics]\nRenderer=metal\nRenderDistance=near\nUpscale=99\n"
                    "[Testing]\nDefaultProfile=bad/name\n"),
          "write malformed-value fixture");
    const ConfigLoadResult malformed = loadConfigFile(malformedPath);
    check(malformed.config.windowMode == KzWindowMode::Borderless && malformed.config.width == 0 &&
              !malformed.config.vsync && malformed.config.renderer == KzRenderer::D3D11 &&
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
    const std::string future = "[Config]\nVersion=2\n[Paths]\nISO=X:\\BYOD\\Future.iso\n";
    check(writeText(futurePath, future), "write future-version fixture");
    const ConfigLoadResult futureLoad = loadConfigFile(futurePath);
    check(hasDiagnostic(futureLoad, DiagnosticLevel::Error, "Config", "Version", "2"),
          "future-version diagnostic");
    check(!saveConfigFile(futurePath, futureLoad.config, false), "future-version save refusal");
    check(readText(futurePath) == future, "future-version file unchanged");

    std::filesystem::remove_all(root, error);
    check(!error, "remove fixture directory");
    return failures;
}

KzConfig &kzConfig()
{
    static KzConfig cfg;
    return cfg;
}
