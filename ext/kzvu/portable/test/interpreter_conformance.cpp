#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "ps2recomp_stubs.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t kCodeSize = 0x4000;
constexpr uint32_t kDataSize = 0x4000;
constexpr uint32_t kUpperNop = 0x000002FFu;
constexpr uint32_t kLowerNop = 0x8000033Cu;
constexpr uint32_t kEBit = 1u << 30;
constexpr uint32_t kTBit = 1u << 27;
constexpr uint32_t kX = 8u;
constexpr uint32_t kZ = 2u;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* message)
{
    ++g_checks;
    if (condition)
        return;
    ++g_failures;
    std::printf("    FAIL: %s\n", message);
}

constexpr uint32_t upper(uint32_t op, uint32_t dest, uint32_t ft, uint32_t fs, uint32_t fd)
{
    return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op;
}

constexpr uint32_t lower(uint32_t op)
{
    return op << 25;
}

constexpr uint32_t lowerSpecial(uint32_t code, uint32_t dest, uint32_t ft, uint32_t fs)
{
    return lower(0x40) | (dest << 21) | (ft << 16) | (fs << 11) |
           ((code >> 2) << 6) | (0x3Cu | (code & 3u));
}

constexpr uint32_t lq(uint32_t dest, uint32_t ft, uint32_t is, int32_t immediate)
{
    return (dest << 21) | (ft << 16) | (is << 11) |
           (static_cast<uint32_t>(immediate) & 0x7FFu);
}

constexpr uint32_t sq(uint32_t dest, uint32_t fs, uint32_t it, int32_t immediate)
{
    return lower(0x01) | (dest << 21) | (it << 16) | (fs << 11) |
           (static_cast<uint32_t>(immediate) & 0x7FFu);
}

constexpr uint32_t iaddiu(uint32_t it, uint32_t is, int32_t immediate)
{
    const uint32_t value = static_cast<uint32_t>(immediate) & 0x7FFFu;
    return lower(0x08) | (((value >> 11) & 0xFu) << 21) |
           (it << 16) | (is << 11) | (value & 0x7FFu);
}

constexpr uint32_t branch(int32_t immediate)
{
    return lower(0x20) | (static_cast<uint32_t>(immediate) & 0x7FFu);
}

constexpr uint32_t xgkick(uint32_t is)
{
    return lowerSpecial(0x6Cu, 0, 0, is);
}

constexpr uint64_t gifTag(uint32_t nloop, bool eop, uint32_t format, uint32_t nreg)
{
    return static_cast<uint64_t>(nloop & 0x7FFFu) |
           (static_cast<uint64_t>(eop) << 15) |
           (static_cast<uint64_t>(format & 3u) << 58) |
           (static_cast<uint64_t>(nreg & 0xFu) << 60);
}

struct Fixture
{
    alignas(64) std::array<uint8_t, kCodeSize> code{};
    alignas(64) std::array<uint8_t, kDataSize> data{};
    std::unique_ptr<PS2Memory> memory;
    VU1Interpreter vu;

    Fixture()
        : memory(recompCreateMemory(code.data(), data.data()))
    {
        reset();
    }

    void reset()
    {
        code.fill(0);
        data.fill(0);
        vu.reset();
        g_recompGifPackets.clear();
        recompMarkCodeModified(memory.get());
    }

    void writePair(uint32_t pair, uint32_t low, uint32_t high)
    {
        const uint32_t offset = pair * 8u;
        std::memcpy(code.data() + offset, &low, sizeof(low));
        std::memcpy(code.data() + offset + sizeof(low), &high, sizeof(high));
    }

    void commitCode()
    {
        recompMarkCodeModified(memory.get());
    }

    void execute(uint32_t maxCycles, uint32_t startPc = 0, uint32_t top = 0, uint32_t itop = 0)
    {
        vu.execute(code.data(), kCodeSize, data.data(), kDataSize, recompGs(), memory.get(),
                   startPc, top, itop, maxCycles);
    }

    void resume(uint32_t maxCycles, uint32_t top = 0, uint32_t itop = 0)
    {
        vu.resume(code.data(), kCodeSize, data.data(), kDataSize, recompGs(), memory.get(),
                  top, itop, maxCycles);
    }
};

void writeQword(std::array<uint8_t, kDataSize>& data, uint32_t qword, const float values[4])
{
    std::memcpy(data.data() + (qword & 0x3FFu) * 16u, values, 16u);
}

void readQword(const std::array<uint8_t, kDataSize>& data, uint32_t qword, float values[4])
{
    std::memcpy(values, data.data() + (qword & 0x3FFu) * 16u, 16u);
}

void testArithmeticFlagsAndStalls()
{
    Fixture fx;
    fx.writePair(0, 0, upper(0x28, kX, 2, 1, 3));
    fx.writePair(1, 0, upper(0x28, kX, 2, 3, 4));
    fx.commitCode();
    fx.vu.state().vf[1][0] = 1.0f;
    fx.vu.state().vf[2][0] = 2.0f;

    fx.execute(5);
    check(fx.vu.state().vf[4][0] == 0.0f,
          "dependent FMAC result must remain pending through its stall");
    fx.resume(3);
    check(fx.vu.state().vf[3][0] == 3.0f, "first ADD result is incorrect");
    check(fx.vu.state().vf[4][0] == 5.0f, "dependent ADD did not consume the completed lane");
    check(fx.vu.state().cycles == 8u, "FMAC dependency should consume eight cycles");

    fx.reset();
    fx.writePair(0, 0, upper(0x28, kX | kZ, 2, 1, 3));
    fx.writePair(1, 0, kUpperNop);
    fx.writePair(2, 0, kUpperNop);
    fx.writePair(3, 0, kUpperNop);
    fx.writePair(4, 0, kUpperNop);
    fx.commitCode();
    fx.vu.state().vf[1][0] = 1.0f;
    fx.vu.state().vf[1][2] = -3.0f;
    fx.vu.state().vf[2][0] = -1.0f;
    fx.vu.state().vf[2][2] = 1.0f;

    fx.execute(3);
    check(fx.vu.state().mac == 0u, "FMAC flags became visible before cycle four");
    fx.resume(2);
    check(fx.vu.state().mac == 0x28u, "FMAC lane flags are incorrect");
    check(fx.vu.state().status == 0xC3u, "FMAC current and sticky status flags are incorrect");
}

void testBranches()
{
    Fixture fx;
    fx.writePair(0, branch(2), kUpperNop);
    fx.writePair(1, iaddiu(1, 0, 1), kUpperNop);
    fx.writePair(2, iaddiu(2, 0, 99), kUpperNop);
    fx.writePair(3, iaddiu(3, 0, 7), kUpperNop);
    fx.commitCode();

    fx.execute(3);
    check(fx.vu.state().vi[1] == 1, "branch delay slot did not execute");
    check(fx.vu.state().vi[2] == 0, "branch failed to skip the sequential instruction");
    check(fx.vu.state().vi[3] == 7, "branch target did not execute");
}

void testMemoryBehavior()
{
    Fixture fx;
    const float source[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    const float destination[4] = {-1.0f, -2.0f, -3.0f, -4.0f};
    writeQword(fx.data, 3, source);
    writeQword(fx.data, 5, destination);
    fx.writePair(0, lq(0x5, 4, 1, 1), kUpperNop);
    fx.writePair(1, sq(0xA, 4, 2, 1), kUpperNop);
    fx.commitCode();
    fx.vu.state().vi[1] = 2;
    fx.vu.state().vi[2] = 4;
    fx.vu.state().vf[4][0] = 100.0f;
    fx.vu.state().vf[4][1] = 200.0f;
    fx.vu.state().vf[4][2] = 300.0f;
    fx.vu.state().vf[4][3] = 400.0f;

    fx.execute(3);
    check(fx.vu.state().vf[4][1] == 200.0f, "LQ write became visible before its fourth cycle");
    fx.resume(1);
    check(fx.vu.state().vf[4][0] == 100.0f && fx.vu.state().vf[4][1] == 20.0f &&
              fx.vu.state().vf[4][2] == 300.0f && fx.vu.state().vf[4][3] == 40.0f,
          "LQ destination mask or qword addressing is incorrect");

    float stored[4]{};
    readQword(fx.data, 5, stored);
    check(stored[0] == 100.0f && stored[1] == -2.0f &&
              stored[2] == 300.0f && stored[3] == -4.0f,
          "SQ destination mask or qword addressing is incorrect");
}

void testXgkick()
{
    Fixture fx;
    constexpr uint32_t lastQword = (kDataSize / 16u) - 1u;
    const uint64_t tag = gifTag(1, true, 2, 0);
    std::memcpy(fx.data.data() + lastQword * 16u, &tag, sizeof(tag));
    for (uint32_t index = 0; index < 16u; ++index)
        fx.data[index] = static_cast<uint8_t>(0xC0u + index);

    fx.writePair(0, xgkick(1), kUpperNop);
    fx.writePair(1, kLowerNop, kUpperNop);
    fx.writePair(2, kLowerNop, kUpperNop);
    fx.commitCode();
    fx.vu.state().vi[1] = static_cast<int32_t>(lastQword);
    fx.execute(3);

    check(g_recompGifPackets.size() == 1u, "XGKICK did not emit exactly one packet");
    if (g_recompGifPackets.size() != 1u)
        return;
    const std::vector<uint8_t>& packet = g_recompGifPackets.front();
    check(packet.size() == 32u, "wrapped XGKICK packet has the wrong size");
    check(packet.size() >= 32u &&
              std::memcmp(packet.data() + 16u, fx.data.data(), 16u) == 0,
          "XGKICK did not wrap its payload across VU1 data memory");
}

void testDecodeInvalidation()
{
    Fixture fx;
    fx.writePair(0, iaddiu(1, 0, 1), kUpperNop);
    fx.commitCode();
    fx.execute(1);
    check(fx.vu.state().vi[1] == 1, "initial decoded instruction is incorrect");

    fx.writePair(0, iaddiu(1, 0, 2), kUpperNop);
    fx.execute(1);
    check(fx.vu.state().vi[1] == 1, "decode cache changed without a code-generation update");

    fx.commitCode();
    fx.execute(1);
    check(fx.vu.state().vi[1] == 2, "code-generation update did not invalidate decoded state");
}

void testLifecycle()
{
    Fixture fx;
    fx.writePair(0, iaddiu(1, 0, 1), kUpperNop);
    fx.writePair(1, iaddiu(2, 0, 2), kUpperNop);
    fx.writePair(2, iaddiu(3, 0, 3), kUpperNop | kEBit);
    fx.writePair(3, iaddiu(4, 0, 4), kUpperNop);
    fx.writePair(4, iaddiu(5, 0, 5), kUpperNop);
    fx.commitCode();

    fx.execute(2);
    check(fx.vu.state().vi[1] == 1 && fx.vu.state().vi[2] == 2,
          "bounded execute did not stop at its cycle budget");
    check(fx.vu.state().vi[3] == 0, "bounded execute ran past its cycle budget");
    fx.resume(64);
    check(fx.vu.state().vi[3] == 3 && fx.vu.state().vi[4] == 4,
          "resume did not execute the E-bit delay slot");
    check(fx.vu.state().vi[5] == 0, "execution continued past the E-bit delay slot");
    check(fx.vu.state().cycles == 4u, "cycle total did not survive execute/resume");

    fx.vu.reset();
    check(fx.vu.state().cycles == 0u && fx.vu.state().vi[1] == 0,
          "reset did not clear interpreter state");
    fx.execute(64);
    check(fx.vu.state().vi[4] == 4 && fx.vu.state().vi[5] == 0,
          "interpreter could not be reused after reset");

    fx.reset();
    fx.writePair(0, iaddiu(1, 0, 1), kUpperNop | kTBit);
    fx.writePair(1, iaddiu(2, 0, 2), kUpperNop | kEBit);
    fx.writePair(2, iaddiu(3, 0, 3), kUpperNop);
    fx.writePair(3, iaddiu(4, 0, 4), kUpperNop);
    fx.commitCode();
    fx.vu.state().tBitEnabled = true;
    fx.execute(64);
    check(fx.vu.state().stoppedByT && fx.vu.state().vi[1] == 1 && fx.vu.state().vi[2] == 0,
          "T-bit stop did not halt before the next pair");
    fx.vu.state().tBitEnabled = false;
    fx.resume(64);
    check(!fx.vu.state().stoppedByT && fx.vu.state().vi[2] == 2 &&
              fx.vu.state().vi[3] == 3 && fx.vu.state().vi[4] == 0,
          "resume after a T-bit stop did not finish at the E-bit delay slot");
}

using TestFn = void (*)();

void run(const char* name, TestFn test)
{
    const int before = g_failures;
    std::printf("[%s]\n", name);
    test();
    std::printf("    %s\n", g_failures == before ? "ok" : "FAILED");
}
}

int main()
{
    run("arithmetic, flags, and stalls", &testArithmeticFlagsAndStalls);
    run("branches and delay slots", &testBranches);
    run("memory masks and latency", &testMemoryBehavior);
    run("XGKICK memory wrapping", &testXgkick);
    run("decode invalidation", &testDecodeInvalidation);
    run("budget, resume, reset, and stop lifecycle", &testLifecycle);

    if (g_failures != 0)
    {
        std::printf("\nkzvu_interpreter_conformance: %d/%d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\nkzvu_interpreter_conformance: %d checks passed\n", g_checks);
    return 0;
}
