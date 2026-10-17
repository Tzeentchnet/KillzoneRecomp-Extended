#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace
{
    int fail(const char *message)
    {
        std::fprintf(stderr, "[kz-host-audio-smoke] FAIL: %s\n", message);
        return 1;
    }

    struct FeedState
    {
        std::atomic<unsigned> callbacks{0};
        std::atomic<bool> failed{false};
    };

    void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int)
    {
        auto &state = *static_cast<FeedState *>(userdata);
        ++state.callbacks;

        std::array<std::byte, 4096> silence{};
        int remaining = std::max(additional, 0);
        while (remaining > 0)
        {
            const int size = std::min<int>(remaining, static_cast<int>(silence.size()));
            if (!SDL_PutAudioStreamData(stream, silence.data(), size))
            {
                state.failed = true;
                return;
            }
            remaining -= size;
        }
    }

    struct SdlAudioSession
    {
        ~SdlAudioSession()
        {
            if (stream)
                SDL_DestroyAudioStream(stream);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }

        SDL_AudioStream *stream = nullptr;
    };
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return fail(SDL_GetError());

    FeedState feedState;
    SdlAudioSession session;
    const SDL_AudioSpec source{SDL_AUDIO_S16, 2, 48000};
    session.stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &source,
        feed,
        &feedState);
    if (!session.stream)
        return fail(SDL_GetError());

    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(session.stream);
    if (device == 0)
        return fail("SDL did not bind the stream to an audio device");

    SDL_AudioSpec deviceSpec{};
    int sampleFrames = 0;
    if (!SDL_GetAudioDeviceFormat(device, &deviceSpec, &sampleFrames))
        return fail(SDL_GetError());
    if (deviceSpec.freq <= 0 || deviceSpec.channels <= 0 || sampleFrames <= 0)
        return fail("SDL reported an invalid audio-device format");

    if (!SDL_ResumeAudioStreamDevice(session.stream))
        return fail(SDL_GetError());

    const Uint64 deadline = SDL_GetTicks() + 2000;
    while (feedState.callbacks.load() == 0 && SDL_GetTicks() < deadline)
        SDL_Delay(1);

    if (!SDL_PauseAudioStreamDevice(session.stream))
        return fail(SDL_GetError());
    if (feedState.failed)
        return fail("the SDL audio callback could not queue silence");
    if (feedState.callbacks.load() == 0)
        return fail("the SDL audio device did not request samples");

    const char *driver = SDL_GetCurrentAudioDriver();
    const char *deviceName = SDL_GetAudioDeviceName(device);
    std::printf(
        "[kz-host-audio-smoke] passed (driver=%s device=%s format=%dHz/%dch buffer=%d callbacks=%u)\n",
        driver ? driver : "?",
        deviceName ? deviceName : "?",
        deviceSpec.freq,
        deviceSpec.channels,
        sampleFrames,
        feedState.callbacks.load());
    return 0;
}
