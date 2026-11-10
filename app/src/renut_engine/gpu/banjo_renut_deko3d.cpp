// banjo-nx (2026-10-17): Renut's renderer on plume's deko3d back end (banjo_renut_deko3d.h, docs/deko3d-renderer.md).
#include "renut_engine/gpu/banjo_renut_deko3d.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <unistd.h>  // ftruncate (LoadCache)

#include <fmt/format.h>
#include <rex/logging.h>
#include <zstd.h>

#include "profiler/profiler.h"  // ffxiii2-nx: the profiler window's renderer counters
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/render/surfaces.h"  // ffxiii2-nx (2026-11-07): dynamic resolution for the overlay
#include "renut_engine/gpu/shaders/shader_cache.h"  // peggle2-nx: MissingGameShaders

#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>

#include <rex/cvar.h>
REXCVAR_DECLARE(bool, log_file_writer);  // peggle2-nx (2026-10-20): deko3d.log only with it (InstallShaderCache); ffxiii2-nx: this SDK names the switch log_file_writer
#endif

// The GLSL package (tools/deko3d_shader_package.py: deko3d_glsl_switch.cpp embeds it); absent from builds made without
// one, hence weak.
extern const uint8_t g_deko3dGlslPackage[] __attribute__((weak));
extern const size_t g_deko3dGlslPackageSize __attribute__((weak));

namespace banjo::renut_gpu::deko3d {

namespace {

#pragma pack(push, 1)
struct PackageHeader {
  char magic[4];
  uint32_t version, count, dictSize, tableOffset, dataOffset, reserved[2];
};
struct PackageEntry {
  uint64_t hash;
  uint32_t offset, compressedSize, size, stage;
  uint32_t boolMask[8];
};
#pragma pack(pop)
static_assert(sizeof(PackageEntry) == 56, "tools/deko3d_shader_package.py's entry");

// One GLSL package: the one built into the NRO, or (peggle2-nx, 2026-10-20) deko3d_glsl.bin beside it, which the web
// installer makes from the player's own default.xex (a build for distribution carries only the port's own shaders).
struct Package {
  bool ok = false;
  std::vector<uint8_t> file;  // the file's bytes (the built-in package is used in place)
  const uint8_t *base = nullptr;
  size_t size = 0;
  const PackageHeader *header = nullptr;
  const PackageEntry *entries = nullptr;
  const uint8_t *data = nullptr;
  ZSTD_DDict *dict = nullptr;  // none when the package has no dictionary (the installer's: raw zstd frames)
  std::mutex mutex;            // the decompression context
  ZSTD_DCtx *dctx = nullptr;
  // ffxiii2-nx (2026-11-05, the complete shader set: 24,920 shaders, 80 MB of compressed GLSL): the package beside the
  // NRO is no longer read whole. Header, dictionary and table (about 1.5 MB) stay in memory; each shader's compressed
  // text is read from the open file when a variant of it is compiled (CreateGameShader, under the mutex). The process
  // had 4 MB to spare (3185 of 3189 MB, the profiler window on the console).
  std::FILE *stream = nullptr;
  uint64_t stream_data = 0;  // file offset of the data section
  std::vector<uint8_t> chunk;
};

// Index 0: beside the NRO (looked up first), 1: built in.
struct Packages {
  std::once_flag once;
  Package source[2];
};
Packages g_packages;

bool Attach(Package &p, const uint8_t *base, size_t size, const char *what) {
  if (size < sizeof(PackageHeader))
    return false;
  const auto *h = reinterpret_cast<const PackageHeader *>(base);
  const uint64_t table_end = uint64_t(h->tableOffset) + uint64_t(h->count) * sizeof(PackageEntry);
  if (std::memcmp(h->magic, "BDKG", 4) != 0 || h->version != 1 || sizeof(PackageHeader) + uint64_t(h->dictSize) > size ||
      table_end > size || h->dataOffset > size) {
    REXLOG_ERROR("[deko3d] {}: not a GLSL package this build reads", what);
    return false;
  }
  const auto *entries = reinterpret_cast<const PackageEntry *>(base + h->tableOffset);
  for (uint32_t i = 0; i < h->count; ++i) {
    if (uint64_t(h->dataOffset) + entries[i].offset + entries[i].compressedSize > size ||
        (i && entries[i - 1].hash >= entries[i].hash)) {
      REXLOG_ERROR("[deko3d] {}: entry {} out of bounds or out of order", what, i);
      return false;
    }
  }
  p.base = base;
  p.size = size;
  p.header = h;
  p.entries = entries;
  p.data = base + h->dataOffset;
  p.dict = h->dictSize ? ZSTD_createDDict(base + sizeof(PackageHeader), h->dictSize) : nullptr;
  p.dctx = ZSTD_createDCtx();
  p.ok = p.dctx && (p.dict || !h->dictSize);
  return p.ok;
}

Packages &Open() {
  std::call_once(g_packages.once, [] {
    Packages &all = g_packages;
    // peggle2-nx: deko3d_glsl.bin beside the NRO first.
    const std::string path = NextToExecutable("deko3d_glsl.bin").string();
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
      Package &p = all.source[0];
      std::fseek(f, 0, SEEK_END);
      const long n = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      // ffxiii2-nx: only what precedes the data section (header, dictionary, table); the data stays in the file.
      PackageHeader h{};
      size_t head = 0;
      if (n > long(sizeof(PackageHeader)) && std::fread(&h, 1, sizeof(h), f) == sizeof(h) &&
          std::memcmp(h.magic, "BDKG", 4) == 0 && h.dataOffset <= uint64_t(n))
        head = h.dataOffset;
      if (head) {
        p.file.resize(head);
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(p.file.data(), 1, head, f) != head)
          p.file.clear();
      }
      // Attach checks every entry against the whole file's size; p.data then points past the buffer and is only used
      // through the stream (CreateGameShader).
      if (!p.file.empty() && Attach(p, p.file.data(), size_t(n), path.c_str())) {
        p.stream = f;
        p.stream_data = h.dataOffset;
        p.data = nullptr;
        REXLOG_INFO("[deko3d] GLSL package {}: {} shaders, {} KB (index {} KB in memory, the shaders read on use)",
                    path, p.header->count, uint64_t(n) >> 10, head >> 10);
      } else {
        std::fclose(f);
      }
    }
    if (&g_deko3dGlslPackageSize != nullptr && g_deko3dGlslPackageSize >= sizeof(PackageHeader) &&
        Attach(all.source[1], g_deko3dGlslPackage, g_deko3dGlslPackageSize, "the NRO's GLSL package"))
      REXLOG_INFO("[deko3d] GLSL package: {} shaders, {} KB in the NRO", all.source[1].header->count,
                  g_deko3dGlslPackageSize >> 10);
    if (!all.source[0].ok && !all.source[1].ok)
      REXLOG_ERROR("[deko3d] no GLSL package (deko3d_glsl.bin beside the NRO, or built in): no game shaders");
  });
  return g_packages;
}

struct Found {
  Package *package = nullptr;
  const PackageEntry *entry = nullptr;
};

Found Find(Packages &all, uint64_t hash) {
  for (Package &p : all.source) {
    if (!p.ok)
      continue;
    const PackageEntry *begin = p.entries, *end = p.entries + p.header->count;
    const PackageEntry *it =
        std::lower_bound(begin, end, hash, [](const PackageEntry &e, uint64_t h) { return e.hash < h; });
    if (it != end && it->hash == hash)
      return {&p, it};
  }
  return {};
}

// ---- the DKSH cache next to the NRO: "BDKC", version, the compiler's tag, then records (u64 key, u32 size, DKSH)
// uam 1.1.0 with SwitchWakerHD's 8 patches and banjo-nx's passes (bnx3, 2026-10-18: with the value numbering).
// banjo-nx (2026-10-20): bnx4 with uam banjo flag 1024 (the 128-bit bone rows) in the default flags: a cache
// compiled before is another compiler's.
constexpr char kCacheTag[16] = "uam110-sw8-bnx4";

constexpr uint32_t kDkshMagic = 0x48534B44u;  // "DKSH", the first word of every record's data

struct DkshCache {
  std::mutex mutex;
  FILE *file = nullptr;
  std::unordered_map<uint64_t, std::pair<long, uint32_t>> index;  // key -> offset of the DKSH, size
  long end = 0;  // the end of the last whole record: where the next one is written
  bool read_only = false;  // a write failed: no more records
  uint64_t added = 0, hits = 0, misses = 0;
  std::string path;
};
DkshCache g_cache;

// ffxiii2-nx (2026-11-08, k252, the user: newer builds boot slower): the record walk below costs one seek and three small
// reads per record; with rc9's 62,375 variants (505 MB) that was 25 s on the SD before the device was made (0.7 s with
// rc2's 2,031). The walk's result is kept next to the cache (deko3d_shaders.bin.idx: "BDKI", version, the compiler's
// tag, the cache length it covers, the count, then u64 key / u32 offset of the DKSH / u32 size per record, 1 MB), read
// in one go at the next start; only records after the covered length are walked. Checked before use: the tag, the
// covered length within the file, the file's size against the count, and 33 records' headers (key, size, magic) read
// from the cache; any mismatch and the whole cache is walked as before. Every read of a DKSH also checks its record's
// key and size (a cache replaced under an old index cannot hand the wrong shader). The cache itself is unchanged.
constexpr uint32_t kIndexVersion = 1;
constexpr size_t kIndexHead = 4 + 4 + 16 + 8 + 8;

struct IndexEntry {
  uint64_t key;
  uint32_t offset, size;
};
static_assert(sizeof(IndexEntry) == 16);

// The records the index file lists (into c.index); returns the length they cover, or 0 when it cannot be used.
long LoadCacheIndex(DkshCache &c, long length, const char *tag) {
  const std::string path = c.path + ".idx";
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return 0;
  char head[kIndexHead] = {};
  uint32_t version = 0;
  uint64_t covered = 0, count = 0;
  std::vector<IndexEntry> entries;
  bool ok = std::fread(head, 1, sizeof(head), f) == sizeof(head) && std::memcmp(head, "BDKI", 4) == 0;
  if (ok) {
    std::memcpy(&version, head + 4, 4);
    std::memcpy(&covered, head + 24, 8);
    std::memcpy(&count, head + 32, 8);
    ok = version == kIndexVersion && std::memcmp(head + 8, tag, 16) == 0 && covered >= 24 &&
         covered <= uint64_t(length) && count <= (64u << 20) / sizeof(IndexEntry);
  }
  if (ok) {
    std::fseek(f, 0, SEEK_END);
    ok = uint64_t(std::ftell(f)) == kIndexHead + count * sizeof(IndexEntry);
  }
  if (ok) {
    entries.resize(size_t(count));
    ok = std::fseek(f, long(kIndexHead), SEEK_SET) == 0 &&
         (count == 0 || std::fread(entries.data(), sizeof(IndexEntry), entries.size(), f) == entries.size());
  }
  std::fclose(f);
  if (!ok)
    return 0;
  // 33 records checked against the cache (every 32nd of the list and the last).
  for (size_t s = 0; s <= 32 && !entries.empty(); ++s) {
    const IndexEntry &e = entries[s == 32 ? entries.size() - 1 : entries.size() * s / 32];
    uint64_t key = 0;
    uint32_t size = 0, magic = 0;
    if (e.offset < 36 || uint64_t(e.offset) + e.size > covered || std::fseek(c.file, long(e.offset) - 12, SEEK_SET) != 0 ||
        std::fread(&key, sizeof(key), 1, c.file) != 1 || std::fread(&size, sizeof(size), 1, c.file) != 1 ||
        std::fread(&magic, sizeof(magic), 1, c.file) != 1 || key != e.key || size != e.size || magic != kDkshMagic)
      return 0;
  }
  c.index.reserve(entries.size() + 1024);
  for (const IndexEntry &e : entries)
    c.index[e.key] = {long(e.offset), e.size};
  return long(covered);
}

// The index of c.index up to c.end, written to a temporary file and moved over the old one.
bool WriteCacheIndex(const DkshCache &c, const char *tag) {
  const std::string path = c.path + ".idx", temp = path + ".new";
  std::vector<IndexEntry> entries;
  entries.reserve(c.index.size());
  for (const auto &[key, at] : c.index)
    if (at.first > 0 && at.first + long(at.second) <= c.end && at.first < long(UINT32_MAX))
      entries.push_back({key, uint32_t(at.first), at.second});
  std::sort(entries.begin(), entries.end(), [](const IndexEntry &a, const IndexEntry &b) { return a.offset < b.offset; });
  char head[kIndexHead] = {'B', 'D', 'K', 'I'};
  const uint64_t covered = uint64_t(c.end), count = entries.size();
  std::memcpy(head + 4, &kIndexVersion, 4);
  std::memcpy(head + 8, tag, 16);
  std::memcpy(head + 24, &covered, 8);
  std::memcpy(head + 32, &count, 8);
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  bool ok = std::fwrite(head, 1, sizeof(head), f) == sizeof(head) &&
            (entries.empty() || std::fwrite(entries.data(), sizeof(IndexEntry), entries.size(), f) == entries.size()) &&
            std::fflush(f) == 0;
  if (ok)
    fsync(fileno(f));
  std::fclose(f);
  if (!ok) {
    std::remove(temp.c_str());
    return false;
  }
  std::remove(path.c_str());  // the Switch's FS does not rename over a file
  return std::rename(temp.c_str(), path.c_str()) == 0;
}

void LoadCache() {
  DkshCache &c = g_cache;
  // banjo-nx (2026-10-18): the code uam makes with other flags (banjo_deko3d_uam_prmt, banjo_deko3d_uam_gvn off) has a
  // cache of its own.
  const unsigned flags = Deko3DUamFlags();
  c.path = NextToExecutable(flags == 1471u ? std::string("deko3d_shaders.bin")
                                          : "deko3d_shaders_f" + std::to_string(flags) + ".bin").string();
  c.file = std::fopen(c.path.c_str(), "r+b");
  bool fresh = c.file == nullptr;
  if (c.file) {
    char head[24] = {};
    if (std::fread(head, 1, sizeof(head), c.file) != sizeof(head) || std::memcmp(head, "BDKC", 4) != 0 ||
        std::memcmp(head + 8, kCacheTag, sizeof(kCacheTag)) != 0) {
      REXLOG_INFO("[deko3d] DKSH cache {}: another compiler's or unreadable; starting it over", c.path);
      std::fclose(c.file);
      c.file = nullptr;
      fresh = true;
    }
    else {
      std::fseek(c.file, 0, SEEK_END);
      const long length = std::ftell(c.file);
      // ffxiii2-nx (2026-11-08, k252): the index file's records first; the walk only covers what follows them.
      const auto t0 = std::chrono::steady_clock::now();
      const long indexed = LoadCacheIndex(c, length, kCacheTag);
      const size_t from_index = c.index.size();
      // Records up to the first that is not whole: its size runs past the end, or its data is not a DKSH (an append
      // the process did not finish). The file is cut there and new records follow the good ones. Before, appends
      // went after such a record, where the next start's walk never found them: Ryujinx runs 20-22 compiled the same
      // ~30 shaders on every start.
      long at = indexed > 0 ? indexed : long(sizeof(head));
      for (;;) {
        uint64_t key;
        uint32_t size, magic;
        if (at + 16 > length || std::fseek(c.file, at, SEEK_SET) != 0 || std::fread(&key, sizeof(key), 1, c.file) != 1 ||
            std::fread(&size, sizeof(size), 1, c.file) != 1 || std::fread(&magic, sizeof(magic), 1, c.file) != 1)
          break;
        if (size < 64 || size > (16u << 20) || at + 12 + long(size) > length || magic != kDkshMagic)
          break;
        c.index[key] = {at + 12, size};
        at += 12 + long(size);
      }
      c.end = at;
      const size_t walked = c.index.size() - from_index;
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      bool written = false;
      if (indexed == 0 || walked > 0)
        written = WriteCacheIndex(c, kCacheTag);
      REXLOG_INFO("[deko3d] DKSH cache index: {} records from {}.idx{}, {} walked in the cache, {:.0f} ms{}", from_index,
                  c.path, indexed > 0 ? "" : " (none or not usable)", walked, ms,
                  written ? "; index written" : (indexed == 0 || walked > 0) ? "; index NOT written" : "");
      if (at != length) {
        REXLOG_WARN("[deko3d] DKSH cache {}: {} bytes after the last whole record ({} of them) dropped", c.path,
                    length - at, c.index.size());
        std::fflush(c.file);
        if (ftruncate(fileno(c.file), at) != 0)
          REXLOG_WARN("[deko3d] DKSH cache {}: could not be cut; new records overwrite the rest", c.path);
      }
    }
  }
  if (fresh) {
    c.file = std::fopen(c.path.c_str(), "w+b");
    if (c.file) {
      char head[24] = {'B', 'D', 'K', 'C', 1, 0, 0, 0};
      std::memcpy(head + 8, kCacheTag, sizeof(kCacheTag));
      std::fwrite(head, 1, sizeof(head), c.file);
      std::fflush(c.file);
      c.end = sizeof(head);
    }
  }
  REXLOG_INFO("[deko3d] DKSH cache {}: {} variants{}", c.path, c.index.size(), c.file ? "" : " (cannot write it)");
}

}  // namespace

bool PackageAvailable() {
  Packages &all = Open();
  return all.source[0].ok || all.source[1].ok;
}

#if defined(PLUME_DEKO3D)

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *device, uint64_t hash) {
  Packages &all = Open();
  const Found found = Find(all, hash);
  if (!found.entry || !device) {
    static int logged = 0;
    if ((all.source[0].ok || all.source[1].ok) && logged++ < 16)
      REXLOG_ERROR("[deko3d] shader {:016X} is not in the GLSL package (deko3d_glsl.bin beside the NRO: made by the "
                   "installer from your game files?)", hash);
    return nullptr;
  }
  Package &p = *found.package;
  const PackageEntry *e = found.entry;
  std::vector<uint8_t> blob(sizeof(plume::Deko3DShaderBlob) + e->size);
  plume::Deko3DShaderBlob header;
  header.stage = e->stage;
  header.glslSize = e->size;
  header.dkshSize = 0;
  std::memcpy(blob.data(), &header, sizeof(header));
  {
    std::lock_guard<std::mutex> lock(p.mutex);
    const uint8_t *src = p.data ? p.data + e->offset : nullptr;
    if (p.stream) {  // ffxiii2-nx: the package beside the NRO, read on use
      p.chunk.resize(e->compressedSize);
      if (std::fseek(p.stream, long(p.stream_data + e->offset), SEEK_SET) != 0 ||
          std::fread(p.chunk.data(), 1, p.chunk.size(), p.stream) != p.chunk.size()) {
        REXLOG_ERROR("[deko3d] shader {:016X}: could not read its GLSL from the package file", hash);
        return nullptr;
      }
      src = p.chunk.data();
    }
    const size_t n = p.dict ? ZSTD_decompress_usingDDict(p.dctx, blob.data() + sizeof(header), e->size,
                                                         src, e->compressedSize, p.dict)
                            : ZSTD_decompressDCtx(p.dctx, blob.data() + sizeof(header), e->size, src,
                                                  e->compressedSize);
    if (ZSTD_isError(n) || n != e->size) {
      REXLOG_ERROR("[deko3d] shader {:016X}: the GLSL did not unpack ({})", hash,
                   ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short");
      return nullptr;
    }
  }
  auto shader = device->createShader(blob.data(), blob.size(), "main", plume::RenderShaderFormat::DEKO3D);
  if (shader)
    shader->setName(fmt::format("{}_{:016X}", e->stage == 4 ? "ps" : "vs", hash));
  return shader;
}

static void FillOverlayCounters(banjo::profiler::ExtraCounters &c);

void InstallShaderCache() {
  banjo::profiler::SetRendererCounters(&FillOverlayCounters);
  // The back end's messages into this log (plume writes them to stderr otherwise), and into deko3d.log next to the NRO,
  // flushed at each line: the log's buffer is lost when the process dies.
  // peggle2-nx (2026-10-20): only with log_file_writer (the release's built-in settings turn it off: no log files).
  static FILE *diag =
      REXCVAR_GET(log_file_writer) ? std::fopen(NextToExecutable("deko3d.log").string().c_str(), "w") : nullptr;
  static std::mutex diag_mutex;
  plume::SetDeko3DLogCallback([](const char *message) {
    REXLOG_INFO("[deko3d] {}", message);
    std::lock_guard<std::mutex> lock(diag_mutex);
    if (diag) {
      std::fprintf(diag, "%s\n", message);
      std::fflush(diag);
    }
  });
  // banjo-nx (2026-10-18, GPU bandwidth): depth compression and Zcull, which the device and its textures take.
  plume::Deko3DOptions options;
  options.depthCompression = Deko3DDepthCompressionOn();
  options.swapchainCompression = Deko3DSwapchainCompressionOn();
  options.tiledCache = Deko3DTiledCache();  // ffxiii2-nx (2026-11-06, diagnostic)
  options.fullBarriers = Deko3DFullBarriersOn();  // ffxiii2-nx (2026-11-06, diagnostic: D-003)
  options.copyWait = Deko3DCopyWaitOn();          // ffxiii2-nx (2026-11-06, D-003 fix)
  options.zcull = Deko3DZcullOn();
  options.vsOutputs = Deko3DVsOutputsOn();
  options.uamFlags = Deko3DUamFlags();
  options.mergeBarriers = Deko3DBarrierMergeOn();  // banjo-nx (2026-10-19, GPU)
  options.leanBarriers = Deko3DBarrierLeanOn();
  // The uam thread on the background compile core, below the game's threads (2026-10-18).
  options.compileCore = Deko3DCompileCore();
  options.compilePriority = Deko3DCompilePriority();
  // ffxiii2-nx (2026-11-05, from renut-nx): background compiles with the generic variant meanwhile, cache reads off the
  // render thread, boost while a draw waits, the background compiles' share of core 2.
  options.asyncVariants = Deko3DAsyncVariantsOn();
  options.asyncCacheReads = Deko3DAsyncCacheReadsOn();
  options.compileWaitBoost = Deko3DCompileWaitBoostOn();
  options.backgroundCompileShare = Deko3DBackgroundCompileShare();
  plume::Deko3DSetOptions(options);
  LoadCache();
  // The index only (asyncCacheReads), as renut-nx.
  plume::SetDeko3DShaderCacheContains([](uint64_t key) {
    DkshCache &c = g_cache;
    std::lock_guard<std::mutex> lock(c.mutex);
    return c.file != nullptr && c.index.count(key) != 0;
  });
  plume::SetDeko3DShaderCache(
      [](uint64_t key, std::vector<uint8_t> &dksh) {
        DkshCache &c = g_cache;
        std::lock_guard<std::mutex> lock(c.mutex);
        auto it = c.index.find(key);
        if (it == c.index.end() || !c.file) {
          c.misses++;
          return false;
        }
        dksh.resize(it->second.second);
        uint32_t magic = 0;
        // ffxiii2-nx (2026-11-08, k252): the record's own key and size first (the offset may come from the index file).
        uint64_t record_key = 0;
        uint32_t record_size = 0;
        if (it->second.first < 12 || std::fseek(c.file, it->second.first - 12, SEEK_SET) != 0 ||
            std::fread(&record_key, sizeof(record_key), 1, c.file) != 1 ||
            std::fread(&record_size, sizeof(record_size), 1, c.file) != 1 || record_key != key ||
            record_size != it->second.second ||
            std::fread(dksh.data(), 1, dksh.size(), c.file) != dksh.size() ||
            (std::memcpy(&magic, dksh.data(), sizeof(magic)), magic != kDkshMagic)) {
          c.misses++;
          c.index.erase(it);
          return false;
        }
        c.hits++;
        return true;
      },
      [](uint64_t key, const std::vector<uint8_t> &dksh) {
        DkshCache &c = g_cache;
        std::lock_guard<std::mutex> lock(c.mutex);
        if (!c.file || c.read_only || c.index.count(key) || dksh.size() < 64)
          return;
        // One write of the whole record at the end of the last whole one, then flushed: a record cut short (the game
        // closed while writing) is the last one, and the next start drops it (LoadCache).
        const uint32_t size = uint32_t(dksh.size());
        std::vector<uint8_t> record(12 + dksh.size());
        std::memcpy(record.data(), &key, sizeof(key));
        std::memcpy(record.data() + 8, &size, sizeof(size));
        std::memcpy(record.data() + 12, dksh.data(), dksh.size());
        if (std::fseek(c.file, c.end, SEEK_SET) == 0 &&
            std::fwrite(record.data(), 1, record.size(), c.file) == record.size() && std::fflush(c.file) == 0) {
          // peggle2-nx (2026-10-20): fflush only hands the record to the FS service; fsync (libnx: fsFileFlush) has it
          // written out. Without it the last records stayed in Ryujinx's file buffer and were lost when the process was
          // closed (every run compiled the same variant again: 8 of 9 records in the file). Once per new record.
          fsync(fileno(c.file));
          c.index[key] = {c.end + 12, size};
          c.end += long(record.size());
          c.added++;
        }
        else {
          // The SD card full or failing: the records so far keep being read; no more are written (a part of this
          // one may be in the file, after c.end, and the next start drops it).
          REXLOG_WARN("[deko3d] DKSH cache {}: a record could not be written; no more are", c.path);
          c.read_only = true;
        }
      });
}

// ffxiii2-nx (2026-11-05): the profiler window's renderer counters (banjo::profiler::ExtraCounters), cumulative.
static void FillOverlayCounters(banjo::profiler::ExtraCounters &c) {
  const plume::Deko3DStats &st = plume::GetDeko3DStats();
  c.renderer_known = true;
  c.draws = st.draws.load();
  c.pipeline_binds = st.pipelineBinds.load();
  c.shader_compiles = st.shaderCompiles;
  c.shader_compile_failures = st.shaderCompileFailures.load();
  c.shader_compile_avg_ms = c.shader_compiles ? double(st.shaderCompileNs.load()) / 1e6 / double(c.shader_compiles) : 0.0;
  c.cache_hits = g_cache.hits;
  c.cache_added = g_cache.added;
  c.image_mb = st.imageBytes.load() >> 20;
  c.buffer_mb = st.bufferBytes.load() >> 20;
  c.texture_uploads = st.copyUpload.load();
  const renut::gpu::DynamicResolutionInfo dyn = renut::gpu::GetDynamicResolutionInfo();
  c.dynamic_resolution_known = dyn.active;
  c.render_height = dyn.height;
  for (int i = 0; i < 3; ++i) {
    c.dynamic_heights[i] = dyn.heights[i];
    c.dynamic_frames_at[i] = dyn.frames_at[i];
  }
  c.dynamic_changes = dyn.changes;
  c.dynamic_budget_ms = dyn.budget_ms;
  c.dynamic_gpu_avg_ms = dyn.gpu_avg_ms;
}

std::string Report() {
  const plume::Deko3DStats &st = plume::GetDeko3DStats();
  const uint64_t compiles = st.shaderCompiles;
  return fmt::format("deko3d: {} draws ({} through the address window), {} pipeline binds ({} state binds); uam {} compiles (avg {:.0f} ms, {} failed), "
                     "DKSH cache {} hits, {} added; memory: images {} MB, buffers {} MB, code {} KB, commands {} KB; "
                     "barriers: {} primitives, {} fragments, {} full ({} invalidating textures); copy and 2D engines: {} "
                     "buffer copies, {} texture uploads, {} readbacks, {} image copies, {} resolves, {} blits",
                     st.draws.load(), st.windowDraws.load(), st.pipelineBinds.load(), st.stateBinds.load(), compiles,
                     compiles ? double(st.shaderCompileNs.load()) / 1e6 / double(compiles) : 0.0,
                     st.shaderCompileFailures.load(), g_cache.hits, g_cache.added, st.imageBytes.load() >> 20,
                     st.bufferBytes.load() >> 20, st.codeBytes.load() >> 10, st.commandBytes.load() >> 10,
                     st.barriersPrimitives.load(), st.barriersFragments.load(), st.barriersFull.load(),
                     st.barriersImageInvalidate.load(), st.copyBuffer.load(), st.copyUpload.load(), st.copyReadback.load(),
                     st.copyImage.load(), st.copyResolve.load(), st.copyBlit.load());
}

#else

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *, uint64_t) { return nullptr; }
void InstallShaderCache() {}
std::string Report() { return {}; }

#endif

size_t MissingGameShaders() {
  Packages &all = Open();
  size_t missing = 0;
  for (size_t i = 0; i < g_shaderCacheEntryCount; ++i)
    missing += Find(all, g_shaderCacheEntries[i].hash).entry ? 0 : 1;
  return missing;
}

bool BoolSpecMask(uint64_t hash, uint32_t mask[8]) {
  const Found found = Find(Open(), hash);
  if (!found.entry)
    return false;
  std::memcpy(mask, found.entry->boolMask, sizeof(found.entry->boolMask));
  return true;
}

void SaveShaderCache() {
  std::lock_guard<std::mutex> lock(g_cache.mutex);
  if (g_cache.file)
    std::fflush(g_cache.file);
}

}  // namespace banjo::renut_gpu::deko3d
