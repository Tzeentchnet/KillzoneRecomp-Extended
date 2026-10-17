#pragma once
#include <cstdint>
#include <vector>
class GS;
class PS2Memory;
extern std::vector<std::vector<uint8_t>> g_recompGifPackets;
GS& recompGs();
PS2Memory* recompCreateMemory(uint8_t* vu1Code, uint8_t* vu1Data);
void recompMarkCodeModified(PS2Memory* m);
