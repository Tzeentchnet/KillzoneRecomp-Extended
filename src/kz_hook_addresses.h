#pragma once

#include <cstdint>

namespace kz_hook
{
    inline constexpr uint32_t GameEntry = 0x00146F30u;
    inline constexpr uint32_t FileReadAsync = 0x0014D790u;
    inline constexpr uint32_t FilePoll = 0x0014DAD0u;
    inline constexpr uint32_t TimelineKick = 0x00150090u;
    inline constexpr uint32_t TimelineKickCheck = 0x00151FC8u;
    inline constexpr uint32_t TimelineVblank = 0x00152018u;
    inline constexpr uint32_t GlowPass = 0x0015DFC0u;
    inline constexpr uint32_t FrameBlend = 0x0015F768u;
    inline constexpr uint32_t TimelineWaitD1Idle = 0x001759C0u;
    inline constexpr uint32_t TimelineWaitVblankKick = 0x001759F8u;
    inline constexpr uint32_t LensBlur = 0x001A9308u;
    inline constexpr uint32_t SwitchToMenu = 0x001A9738u;
    inline constexpr uint32_t VirtualKeyboard = 0x001B0DF0u;
    inline constexpr uint32_t VirtualKeyboardAddChar = 0x001B17A0u;
    inline constexpr uint32_t VirtualKeyboardBackspace = 0x001B18D8u;
    inline constexpr uint32_t PadButtonValue = 0x001B33B8u;
    inline constexpr uint32_t SetLevelFile = 0x001B89A8u;
    inline constexpr uint32_t TimelineFrameLimiter = 0x001BFF10u;
    inline constexpr uint32_t LodScale = 0x001C2168u;
    inline constexpr uint32_t ProfileMaxDifficulty = 0x001D7118u;
    inline constexpr uint32_t ProfileLevelUnlocked = 0x001D7190u;
    inline constexpr uint32_t ProfileAnimaticUnlocked = 0x001D7258u;
    inline constexpr uint32_t PostRender = 0x001EF640u;
    inline constexpr uint32_t ApplyLook = 0x0021C550u;
    inline constexpr uint32_t LookAcceleration = 0x0023E8C8u;
    inline constexpr uint32_t PitchAutoCentre = 0x0023E9C8u;
    inline constexpr uint32_t ControllerUpdate = 0x0023EE08u;
    inline constexpr uint32_t SifInitRpc = 0x002B3158u;
    inline constexpr uint32_t SifStopModule = 0x002B5AF0u;
    inline constexpr uint32_t SifUnloadModule = 0x002B5CF8u;
    inline constexpr uint32_t SifSearchModuleByName = 0x002B5D88u;
    inline constexpr uint32_t SifLoadModule = 0x002B6060u;
    inline constexpr uint32_t McInit = 0x002BE788u;
    inline constexpr uint32_t McGetInfo = 0x002BF180u;
    inline constexpr uint32_t McFormat = 0x002BF458u;
    inline constexpr uint32_t MovieRead = 0x002DEB98u;
    inline constexpr uint32_t MovieOpen = 0x002DED00u;
    inline constexpr uint32_t MovieInputEnd = 0x002DF800u;
    inline constexpr uint32_t MovieFeed = 0x002E0AC0u;
    inline constexpr uint32_t MovieAudioFeed = 0x002E19C0u;
    inline constexpr uint32_t MovieAudioFull = 0x002E1A40u;
    inline constexpr uint32_t ZoneParams = 0x00339C38u;
    inline constexpr uint32_t MpegGetPicture = 0x003CD750u;
    inline constexpr uint32_t MpegClearRefBuff = 0x003CD9F8u;
    inline constexpr uint32_t KeyboardModuleInit = 0x003D7490u;
    inline constexpr uint32_t StreamOpen = 0x00496168u;
    inline constexpr uint32_t StreamRead = 0x004963B8u;
}
