// ffxiii2-nx (2026-11-03): the interface zumasrevenge-nx's Direct3D layer (render/textures.cpp) expects of its HD atlas
// pack (Peggle 2's PS4 art, zumasrevenge-nx's src/me/zumasrevenge_hd_pack.h). FFXIII-2 has no such pack: never wanted,
// so the layer uploads the game's own textures, as without the pack file there.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zumasrevenge::hd {

struct Entry {
  uint64_t key;
  uint64_t offset;
  uint64_t hash;
  uint32_t raw_bytes;
  uint32_t chunks;
  uint16_t guest_width, guest_height, hd_width, hd_height;
};
static_assert(sizeof(Entry) == 40);

inline bool Wanted(uint32_t) { return false; }
inline const Entry* Find(uint64_t, uint32_t, uint32_t) { return nullptr; }
inline bool Read(const Entry&, uint8_t*, size_t) { return false; }

}  // namespace zumasrevenge::hd
