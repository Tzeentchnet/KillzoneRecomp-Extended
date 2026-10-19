#include "kz_testing.h"

#include "kz_config.h"
#include "kz_hook_addresses.h"
#include "kz_input.h"
#include "kz_timing.h"

#include "ps2_runtime.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Testing options (docs/findings.md "Testing options"). Each one is off by default and installs no hook while off.
//
// Intro: main (0x140138) opens the Intro menu set, whose script switches screens with SwitchToMenu(menu, const char
// *name, push) (0x1A9738): LanguageOutro -> Logos (about 23 s, not skippable with a button) -> MemoryCardTest ->
// PlayGameIntro (the intro cinematic, about 58 s) -> the front end. The skip keeps every screen except those two: the
// script's "Logos" switch is pointed at its own "MemoryCardTest" name, and the cinematic's movie open fails, which is the
// path every movie takes without the IPU (kz_overrides.cpp). Skipping the whole Intro set instead (OpenMenu id 0) was
// tried and rejected. Known issue (WIN-06): after the cinematic is skipped, loading chapter 11 crashes or corrupts
// textures in about a third of runs; it does not happen with the intro played.
//
// Campaign unlocks: the PlayerProfile script methods (bound at 0x1D3558..) answer the menus. IsLevelUnlocked(profile,
// globalLevel) (0x1D7190) is true for level 1 and for level N when the profile's per-level record +0x2D4[N-2] is > 0;
// GetMaxDifficulty(profile, level) (0x1D7118) returns +0x214[level-1]; IsAnimaticUnlocked(profile, name) (0x1D7258) looks
// the animatic up in the profile. A profile named "Shooterman" (the developers' cheat, compared in these three functions
// only) gets true / 2 / true. The option returns exactly those values without touching the profile, so nothing different
// is ever written to the memory card.
//
// Start in a level: the front end is a chain of script screens (MainMenu -> ScreenGame -> ScreenSelectProfile ->
// MapSelect -> ScreenDifficulty -> ScreenCharacterSelect; ScreenCreateProfile when the card has no profile). The
// autopilot presses through them the way a player would, so the profile, memory card and level start all take the
// game's own path: the SwitchToMenu hook tells the host loop which screen is up, and kzTestingPoll presses pad buttons.
// MapSelect is a carousel that opens on the highest unlocked chapter; with every level unlocked that is chapter 11, and
// each D-pad Up moves one chapter back. The level the script then sets (SetLevelFile, "NN.XXX" with NN the chapter) is
// logged and checked.
namespace
{
    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    constexpr int32_t kDeveloperMaxDifficulty = 2; // what the game returns for the "Shooterman" profile
    // The Intro script's screen names are one string table; "MemoryCardTest" was 0x3F0 bytes before "Logos".
    constexpr uint32_t kMenuNameSearch = 0x4000u;

    constexpr int kChapters = 11;

    PS2Runtime::RecompiledFunction g_origSwitchToMenu = nullptr;
    PS2Runtime::RecompiledFunction g_origMovieOpen = nullptr;
    PS2Runtime::RecompiledFunction g_origSetLevelFile = nullptr;
    bool g_skipIntro = false;
    int g_skipMask = 3; // diagnostic KZ_SKIP_INTRO_MASK: 1 skips the logos, 2 the cinematic (WIN-06)
    bool g_logFrontEnd = false;

    // ---- start-in-level autopilot
    enum class Screen : int
    {
        Other,
        MainMenu,
        Game,
        SelectProfile,
        CreateProfile,
        MapSelect,
        Difficulty,
        CharacterSelect,
    };

    struct ScreenName
    {
        const char *name;
        Screen screen;
    };
    constexpr ScreenName kScreens[] = {
        {"MainMenu", Screen::MainMenu},
        {"ScreenGame", Screen::Game},
        {"ScreenSelectProfile", Screen::SelectProfile},
        {"ScreenCreateProfile", Screen::CreateProfile},
        {"MapSelect", Screen::MapSelect},
        {"ScreenDifficulty", Screen::Difficulty},
        {"ScreenCharacterSelect", Screen::CharacterSelect},
    };

    int g_startLevel = 0;      // 0 = autopilot off
    int g_startDifficulty = 0; // 0 Easy, 1 Normal, 2 Hard
    std::atomic<int> g_screen{static_cast<int>(Screen::Other)};
    std::atomic<uint32_t> g_screenChanges{0}; // bumped by the EE thread on every SwitchToMenu
    std::atomic<int> g_chosenChapter{0};      // from SetLevelFile

    bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

    // Guest time in seconds (vblank count / vblank rate), for the log lines.
    double guestSeconds(PS2Runtime *rt)
    {
        return static_cast<double>(rt->memory().gs().vsyncTick.load(std::memory_order_relaxed)) / kzTimingRate();
    }

    void returnValue(R5900Context *ctx, int32_t v)
    {
        setReturnS32(ctx, v);
        ctx->pc = getRegU32(ctx, 31);
    }

    // The three query methods are replaced as a whole (they only read the profile), so no original runs.
    void levelUnlocked(uint8_t *, R5900Context *ctx, PS2Runtime *) { returnValue(ctx, 1); }
    void maxDifficulty(uint8_t *, R5900Context *ctx, PS2Runtime *) { returnValue(ctx, kDeveloperMaxDifficulty); }
    void animaticUnlocked(uint8_t *, R5900Context *ctx, PS2Runtime *) { returnValue(ctx, 1); }

    // Guest address of the NUL-terminated `text` within `range` bytes of `near`, or 0.
    uint32_t findGuestString(const uint8_t *rdram, uint32_t near, uint32_t range, const char *text)
    {
        const size_t len = std::strlen(text) + 1;
        const uint32_t lo = near > 0x00100000u + range ? near - range : 0x00100000u;
        const uint32_t hi = near + range < 0x02000000u - len ? near + range : 0x02000000u - static_cast<uint32_t>(len);
        for (uint32_t a = lo; a < hi; ++a)
            if (std::memcmp(rdram + a, text, len) == 0 && (a == lo || rdram[a - 1] == 0))
                return a;
        return 0;
    }

    // SwitchToMenu(menu, const char *name, push): KZ_FRONTEND_LOG=1 logs every screen; the intro skip replaces "Logos".
    // A call enters at the function's own address; a resume at an inner label (after a yield) runs the original as is.
    void switchToMenu(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kz_hook::SwitchToMenu)
        {
            const uint32_t str = getRegU32(ctx, 5);
            if (inRam(str) && inRam(str + 64u))
            {
                const char *name = reinterpret_cast<const char *>(rdram + str);
                if (g_skipIntro && (g_skipMask & 1) && std::strcmp(name, "Logos") == 0)
                {
                    if (const uint32_t next = findGuestString(rdram, str, kMenuNameSearch, "MemoryCardTest"))
                    {
                        ctx->r[5] = _mm_set_epi64x(0, static_cast<int32_t>(next)); // $a1, sign-extended like every GPR write
                        std::printf("[kz] t=%.1f testing: logos skipped\n", guestSeconds(rt));
                    }
                    else
                        std::printf("[kz] testing: logos kept (next screen name not found)\n");
                }
                if (g_logFrontEnd)
                    std::printf("[kz] t=%.1f front end: %.63s\n", guestSeconds(rt),
                                reinterpret_cast<const char *>(rdram + getRegU32(ctx, 5)));
                if (g_startLevel > 0)
                {
                    Screen screen = Screen::Other;
                    for (const ScreenName &s : kScreens)
                        if (std::strcmp(name, s.name) == 0)
                            screen = s.screen;
                    g_screen.store(static_cast<int>(screen));
                    g_screenChanges.fetch_add(1);
                }
            }
        }
        g_origSwitchToMenu(rdram, ctx, rt);
    }

    // SetLevelFile(gameInfo, const char *file): the campaign script passes "<chapter>.<name>" ("1.Trenches", "11.OTS").
    void setLevelFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kz_hook::SetLevelFile)
        {
            const uint32_t str = getRegU32(ctx, 5);
            if (inRam(str) && inRam(str + 64u))
            {
                const char *file = reinterpret_cast<const char *>(rdram + str);
                std::printf("[kz] t=%.1f front end: level %.63s\n", guestSeconds(rt), file);
                int chapter = 0, digits = 0;
                for (; digits < 2 && file[digits] >= '0' && file[digits] <= '9'; ++digits)
                    chapter = chapter * 10 + (file[digits] - '0');
                if (digits > 0 && file[digits] == '.')
                    g_chosenChapter.store(chapter);
            }
        }
        g_origSetLevelFile(rdram, ctx, rt);
    }

    // Movie open (vtable 0x52EA78 +0x10): a1 is the file name as a String (pointer to its representation, characters at
    // +0x10). Returning 0 makes the caller skip the movie.
    void movieOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kz_hook::MovieOpen)
        {
            const uint32_t str = getRegU32(ctx, 5);
            uint32_t rep = 0;
            if (inRam(str))
                std::memcpy(&rep, rdram + (str & kRamMask), 4);
            char name[64] = {};
            if (inRam(rep) && inRam(rep + 0x10u + sizeof(name)))
                std::memcpy(name, rdram + rep + 0x10u, sizeof(name) - 1);
            if ((g_skipMask & 2) && std::strstr(name, "Movies/Intro/"))
            {
                std::printf("[kz] t=%.1f testing: intro movie skipped (%s)\n", guestSeconds(rt), name);
                returnValue(ctx, 0);
                return;
            }
        }
        g_origMovieOpen(rdram, ctx, rt);
    }

    bool hook(PS2Runtime &rt, uint32_t addr, PS2Runtime::RecompiledFunction fn, PS2Runtime::RecompiledFunction *orig)
    {
        const PS2Runtime::RecompiledFunction original = rt.hasFunction(addr) ? rt.lookupFunction(addr) : nullptr;
        if (orig)
            *orig = original;
        return original && rt.replaceFunction(addr, fn);
    }

    bool envFlag(const char *name, bool value)
    {
        if (const char *v = std::getenv(name); v && *v)
            return std::atoi(v) != 0;
        return value;
    }

    int envInt(const char *name, int value, int lo, int hi)
    {
        if (const char *v = std::getenv(name); v && *v)
            value = std::atoi(v);
        return value < lo ? lo : (value > hi ? hi : value);
    }

    // One step of the autopilot. Menus only take the D-pad and the face buttons (docs/findings.md "Play-test fixes").
    enum class Op
    {
        Press,          // pulse `button`
        WaitTextOpen,   // wait for the on-screen keyboard (restart the screen's steps if it does not open)
        Type,           // queue `text` into the open keyboard
        WaitTextClosed, // wait until the keyboard has taken the text and closed
        Wait,           // a dialog is opening
    };
    struct Step
    {
        Op op;
        uint16_t button = 0;
        const char *text = nullptr;
    };

    constexpr uint64_t kSettleMs = 1200; // after a screen appears, before the first press (transition animations)
    constexpr uint64_t kPressMs = 150;
    constexpr uint64_t kGapMs = 450;     // between presses: the game must see the button released
    constexpr uint64_t kRetryMs = 5000;  // a Cross-only screen that is still up this long after its press is pressed again
    constexpr uint64_t kTextWaitMs = 5000;
    constexpr char kDefaultProfile[] = "Tester\r"; // name typed when the memory card has no profile, then Enter

    std::vector<Step> stepsFor(Screen screen)
    {
        const Step cross{Op::Press, KZ_PAD_CROSS};
        switch (screen)
        {
        case Screen::MainMenu:        // GAME is highlighted
        case Screen::Game:            // Campaign is highlighted
        case Screen::SelectProfile:   // the first profile, or Create in the "no profiles" dialog
        case Screen::CharacterSelect: // the level's default character
            return {cross};
        case Screen::CreateProfile:   // Edit -> type the name -> down to "Create profile" -> create -> "save to the card?" Yes
            return {cross, {Op::WaitTextOpen}, {Op::Type, 0, kDefaultProfile}, {Op::WaitTextClosed},
                    {Op::Press, KZ_PAD_DOWN}, cross, {Op::Wait}, cross};
        case Screen::MapSelect:
        {
            std::vector<Step> steps(static_cast<size_t>(kChapters - g_startLevel), Step{Op::Press, KZ_PAD_UP});
            steps.push_back(cross);
            return steps;
        }
        case Screen::Difficulty:
        {
            std::vector<Step> steps(static_cast<size_t>(g_startDifficulty), Step{Op::Press, KZ_PAD_DOWN});
            steps.push_back(cross);
            return steps;
        }
        default:
            return {};
        }
    }

    bool retryable(Screen screen)
    {
        return screen == Screen::MainMenu || screen == Screen::Game || screen == Screen::SelectProfile ||
               screen == Screen::CharacterSelect;
    }

    struct Autopilot
    {
        uint32_t seenChanges = 0;
        Screen screen = Screen::Other;
        std::vector<Step> steps;
        size_t next = 0;
        uint64_t nextAt = 0;
        uint64_t stepStarted = 0;
        uint64_t lastPress = 0;
        int retries = 0;
        bool finished = false;
        bool reported = false;
    };
    Autopilot g_pilot;
}

void kzTestingPoll()
{
    Autopilot &p = g_pilot;
    if (g_startLevel <= 0 || p.reported)
        return;
    const uint64_t now = SDL_GetTicks();
    if (p.finished)
    {
        // Character chosen: report whether the script set the requested chapter, then stop for good.
        const int chosen = g_chosenChapter.load();
        if (chosen != 0 || now > p.lastPress + 10000)
        {
            if (chosen == g_startLevel)
                std::printf("[kz] testing: start level: chapter %d, difficulty %d\n", chosen, g_startDifficulty);
            else
                std::printf("[kz] testing: start level: WARNING chapter %d was chosen, not %d\n", chosen, g_startLevel);
            p.reported = true;
        }
        return;
    }
    const uint32_t changes = g_screenChanges.load();
    if (changes != p.seenChanges)
    {
        p.seenChanges = changes;
        p.screen = static_cast<Screen>(g_screen.load());
        p.steps = stepsFor(p.screen);
        p.next = 0;
        p.nextAt = now + kSettleMs;
        p.stepStarted = 0;
        p.retries = 0;
    }
    if (p.steps.empty() || now < p.nextAt)
        return;
    if (p.next >= p.steps.size())
    {
        if (p.screen == Screen::CharacterSelect)
            p.finished = true;
        else if (retryable(p.screen) && now > p.lastPress + kRetryMs && p.retries < 3)
        {
            ++p.retries;
            p.next = 0;
        }
        return;
    }
    const Step &step = p.steps[p.next];
    if (p.stepStarted == 0)
        p.stepStarted = now;
    switch (step.op)
    {
    case Op::Press:
        kzInputPulseButton(step.button, kPressMs);
        p.lastPress = now;
        p.nextAt = now + kPressMs + kGapMs;
        break;
    case Op::WaitTextOpen:
        if (!kzInputTextEntryOpen())
        {
            if (now - p.stepStarted > kTextWaitMs)
                p.next = 0, p.stepStarted = 0; // the keyboard did not open: press Edit again
            else
                p.nextAt = now + 100;
            return;
        }
        p.nextAt = now + kGapMs;
        break;
    case Op::Type:
        kzInputQueueText(step.text);
        p.nextAt = now + kGapMs;
        break;
    case Op::WaitTextClosed:
        if (kzInputTextEntryOpen() && now - p.stepStarted < kTextWaitMs)
        {
            p.nextAt = now + 100;
            return;
        }
        p.nextAt = now + kSettleMs;
        break;
    case Op::Wait:
        p.nextAt = now + kSettleMs;
        break;
    }
    ++p.next;
    p.stepStarted = 0;
}

void kzTestingInstall(PS2Runtime &runtime)
{
    const KzConfig &cfg = kzConfig();
    g_startLevel = envInt("KZ_START_LEVEL", cfg.startLevel, 0, kChapters);
    g_startDifficulty = envInt("KZ_START_DIFFICULTY", cfg.startDifficulty, 0, 2);
    // The autopilot counts chapters from the carousel's start, which is the last chapter only when all are unlocked;
    // the intro is skipped too, since the point is to get into the level.
    if (envFlag("KZ_UNLOCK_ALL", cfg.unlockAllLevels) || g_startLevel > 0)
    {
        const bool ok = hook(runtime, kz_hook::ProfileLevelUnlocked, &levelUnlocked, nullptr) &&
                        hook(runtime, kz_hook::ProfileMaxDifficulty, &maxDifficulty, nullptr) &&
                        hook(runtime, kz_hook::ProfileAnimaticUnlocked, &animaticUnlocked, nullptr);
        std::printf("[kz] testing: all campaign levels unlocked (%s)\n", ok ? "hooks installed" : "HOOK FAILED");
    }
    g_skipIntro = envFlag("KZ_SKIP_INTRO", cfg.skipIntro) || g_startLevel > 0;
    g_logFrontEnd = std::getenv("KZ_FRONTEND_LOG") != nullptr;
    g_skipMask = envInt("KZ_SKIP_INTRO_MASK", 3, 0, 3);
    if (g_skipIntro && !hook(runtime, kz_hook::MovieOpen, &movieOpen, &g_origMovieOpen))
        std::printf("[kz] testing: skip intro: HOOK FAILED\n");
    if ((g_skipIntro || g_logFrontEnd) && !hook(runtime, kz_hook::SwitchToMenu, &switchToMenu, &g_origSwitchToMenu))
        std::printf("[kz] testing: front-end hook FAILED\n");
    if ((g_startLevel > 0 || g_logFrontEnd) && !hook(runtime, kz_hook::SetLevelFile, &setLevelFile, &g_origSetLevelFile))
        std::printf("[kz] testing: level hook FAILED\n");
    if (g_skipIntro)
        std::printf("[kz] testing: skip intro\n");
    if (g_startLevel > 0)
        std::printf("[kz] testing: start in chapter %d, difficulty %d\n", g_startLevel, g_startDifficulty);
}
