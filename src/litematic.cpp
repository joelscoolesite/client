#include "litematic.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

namespace {

// ---------------------------------------------------------------- inflate (RFC 1951), after zlib's puff.c

constexpr size_t kMaxOutput = 512u << 20; // a schematic this big would not fit in memory anyway

struct Inflater {
    const uint8_t* in;
    size_t inSize;
    size_t inPos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;
    bool failed = false;
    std::vector<uint8_t>& out;

    int Bits(int need) {
        uint32_t value = bitBuf;
        while (bitCount < need) {
            if (inPos >= inSize) {
                failed = true;
                return 0;
            }
            value |= static_cast<uint32_t>(in[inPos++]) << bitCount;
            bitCount += 8;
        }
        bitBuf = value >> need;
        bitCount -= need;
        return static_cast<int>(value & ((1u << need) - 1));
    }

    struct Huffman {
        short count[16]{};  // codes of each length
        short symbol[320]{}; // symbols ordered by code
    };

    // Returns 0 for a complete code, > 0 for an incomplete one and < 0 for an invalid one.
    static int Build(Huffman& h, const short* lengths, int n) {
        std::fill(std::begin(h.count), std::end(h.count), short{0});
        for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s]) h.symbol[offs[lengths[s]]++] = static_cast<short>(s);
        return left;
    }

    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= Bits(1);
            if (failed) return -1;
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool Stored() {
        bitBuf = 0;
        bitCount = 0;
        if (inPos + 4 > inSize) return false;
        const unsigned len = in[inPos] | (in[inPos + 1] << 8);
        const unsigned nlen = in[inPos + 2] | (in[inPos + 3] << 8);
        inPos += 4;
        if (len != (~nlen & 0xFFFF) || inPos + len > inSize || out.size() + len > kMaxOutput) return false;
        out.insert(out.end(), in + inPos, in + inPos + len);
        inPos += len;
        return true;
    }

    bool Codes(const Huffman& lencode, const Huffman& distcode) {
        static const short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = Decode(lencode);
            if (symbol < 0 || failed) return false;
            if (symbol < 256) {
                out.push_back(static_cast<uint8_t>(symbol));
            } else if (symbol == 256) {
                return true;
            } else {
                symbol -= 257;
                if (symbol >= 29) return false;
                const size_t len = static_cast<size_t>(lbase[symbol] + Bits(lext[symbol]));
                symbol = Decode(distcode);
                if (symbol < 0 || symbol >= 30 || failed) return false;
                const size_t dist = static_cast<size_t>(dbase[symbol] + Bits(dext[symbol]));
                if (failed || dist > out.size() || out.size() + len > kMaxOutput) return false;
                const size_t from = out.size() - dist;
                for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
            }
        }
    }

    bool Fixed() {
        static Huffman lencode, distcode;
        static const bool built = [] {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            Build(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            Build(distcode, lengths, 30);
            return true;
        }();
        (void)built;
        return Codes(lencode, distcode);
    }

    bool Dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = Bits(5) + 257, ndist = Bits(5) + 1, ncode = Bits(4) + 4;
        if (failed || nlen > 286 || ndist > 30) return false;
        short lengths[320]{};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<short>(Bits(3));
        Huffman lencode, distcode;
        if (Build(lencode, lengths, 19) != 0) return false;
        for (int index = 0; index < nlen + ndist;) {
            int symbol = Decode(lencode);
            if (symbol < 0 || failed) return false;
            if (symbol < 16) {
                lengths[index++] = static_cast<short>(symbol);
                continue;
            }
            short len = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                symbol = 3 + Bits(2);
            } else if (symbol == 17) {
                symbol = 3 + Bits(3);
            } else {
                symbol = 11 + Bits(7);
            }
            if (index + symbol > nlen + ndist) return false;
            while (symbol--) lengths[index++] = len;
        }
        if (lengths[256] == 0) return false;
        const int err = Build(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
        const int derr = Build(distcode, lengths + nlen, ndist);
        if (derr < 0 || (derr > 0 && ndist - distcode.count[0] != 1)) return false;
        return Codes(lencode, distcode);
    }

    bool Run() {
        for (;;) {
            const int last = Bits(1);
            const int type = Bits(2);
            if (failed) return false;
            const bool ok = type == 0 ? Stored() : type == 1 ? Fixed() : type == 2 ? Dynamic() : false;
            if (!ok || failed) return false;
            if (last) return true;
        }
    }
};

// ---------------------------------------------------------------- NBT

enum TagType : uint8_t {
    End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray
};

struct Tag {
    TagType type = End;
    int64_t number = 0;
    std::string text;
    std::vector<int64_t> longs;                              // LongArray (and IntArray)
    std::vector<Tag> items;                                  // List
    std::vector<std::pair<std::string, Tag>> children;       // Compound

    const Tag* Get(const char* name) const {
        for (const auto& [key, child] : children)
            if (key == name) return &child;
        return nullptr;
    }
    int64_t GetNumber(const char* name, int64_t fallback = 0) const {
        const Tag* t = Get(name);
        return t && t->type >= Byte && t->type <= Long ? t->number : fallback;
    }
    std::string GetText(const char* name) const {
        const Tag* t = Get(name);
        return t && t->type == String ? t->text : std::string();
    }
};

struct NbtReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    size_t tags = 0;
    bool failed = false;

    bool Need(size_t n) {
        if (failed || size - pos < n) failed = true;
        return !failed;
    }
    uint64_t Unsigned(int bytes) {
        if (!Need(bytes)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v = (v << 8) | data[pos++];
        return v;
    }
    int32_t I32() { return static_cast<int32_t>(Unsigned(4)); }
    std::string Text() {
        const size_t len = Unsigned(2);
        if (!Need(len)) return {};
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }
    // Arrays and lists: refuse counts that cannot fit in what is left of the file.
    bool Count(int32_t n, size_t minBytes) {
        if (n < 0 || static_cast<size_t>(n) * minBytes > size - pos) failed = true;
        return !failed;
    }

    void Payload(Tag& t, TagType type, int depth) {
        t.type = type;
        // A damaged file must not make us nest deeply or allocate without end.
        if (depth > 64 || ++tags > 4'000'000) {
            failed = true;
            return;
        }
        switch (type) {
        case Byte: t.number = static_cast<int8_t>(Unsigned(1)); break;
        case Short: t.number = static_cast<int16_t>(Unsigned(2)); break;
        case Int: t.number = I32(); break;
        case Long: t.number = static_cast<int64_t>(Unsigned(8)); break;
        case Float: Unsigned(4); break;
        case Double: Unsigned(8); break;
        case String: t.text = Text(); break;
        case ByteArray: {
            const int32_t n = I32();
            if (Count(n, 1)) pos += n;
            break;
        }
        case IntArray:
        case LongArray: {
            const int width = type == IntArray ? 4 : 8;
            const int32_t n = I32();
            if (!Count(n, width)) break;
            t.longs.resize(n);
            for (auto& v : t.longs) v = width == 4 ? static_cast<int32_t>(Unsigned(4)) : static_cast<int64_t>(Unsigned(8));
            break;
        }
        case List: {
            const auto itemType = static_cast<TagType>(Unsigned(1));
            const int32_t n = I32();
            if (itemType > LongArray || (itemType == End && n > 0) || !Count(n, 1)) {
                failed = true;
                break;
            }
            for (int32_t i = 0; i < n && !failed; ++i) {
                t.items.emplace_back();
                Payload(t.items.back(), itemType, depth + 1);
            }
            break;
        }
        case Compound:
            for (;;) {
                const auto childType = static_cast<TagType>(Unsigned(1));
                if (failed || childType == End) break;
                if (childType > LongArray) {
                    failed = true;
                    break;
                }
                std::string name = Text();
                t.children.emplace_back(std::move(name), Tag{});
                Payload(t.children.back().second, childType, depth + 1);
                if (failed) break;
            }
            break;
        default: failed = true; break;
        }
    }
};

struct Vec3i {
    int64_t x, y, z;
};

Vec3i ReadVec(const Tag* t) {
    if (!t) return {0, 0, 0};
    return {t->GetNumber("x"), t->GetNumber("y"), t->GetNumber("z")};
}

bool IsAir(const std::string& id) {
    return id == "minecraft:air" || id == "minecraft:cave_air" || id == "minecraft:void_air";
}

} // namespace

bool Gunzip(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    size_t start = 0;
    if (in.size() >= 18 && in[0] == 0x1F && in[1] == 0x8B) {
        if (in[2] != 8) return false;
        const uint8_t flags = in[3];
        start = 10;
        if (flags & 4) { // FEXTRA
            if (start + 2 > in.size()) return false;
            start += 2 + (in[start] | (in[start + 1] << 8));
        }
        for (const uint8_t bit : {uint8_t{8}, uint8_t{16}}) // FNAME, FCOMMENT: zero-terminated
            if (flags & bit) {
                while (start < in.size() && in[start]) ++start;
                ++start;
            }
        if (flags & 2) start += 2; // FHCRC
        if (start >= in.size()) return false;
    }
    out.clear();
    out.reserve(in.size() * 4);
    Inflater inflater{in.data() + start, in.size() - start, 0, 0, 0, false, out};
    return inflater.Run();
}

bool LoadLitematic(const std::filesystem::path& path, Schematic& out, std::string& error) {
    out = Schematic{};
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Could not open the file.";
        return false;
    }
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::vector<uint8_t> data;
    if (raw.size() >= 2 && raw[0] == 0x1F && raw[1] == 0x8B) {
        if (!Gunzip(raw, data)) {
            error = "The file is damaged (could not decompress it).";
            return false;
        }
    } else {
        data = raw; // uncompressed NBT
    }

    NbtReader reader{data.data(), data.size()};
    const auto rootType = static_cast<TagType>(reader.Unsigned(1));
    reader.Text(); // root name
    Tag root;
    if (rootType == Compound) reader.Payload(root, Compound, 0);
    if (rootType != Compound || reader.failed) {
        error = "This is not a valid NBT file.";
        return false;
    }
    const Tag* regions = root.Get("Regions");
    if (!regions || regions->type != Compound) {
        error = "No regions found: is this a .litematic file?";
        return false;
    }
    if (const Tag* meta = root.Get("Metadata")) {
        out.name = meta->GetText("Name");
        out.author = meta->GetText("Author");
    }
    if (out.name.empty()) out.name = path.stem().string();

    constexpr size_t kMaxBlocks = 4'000'000;
    std::map<std::string, uint16_t> stateIndex;
    struct Placed {
        int64_t x, y, z;
        uint16_t state;
    };
    std::vector<Placed> placed;
    int64_t minX = INT64_MAX, minY = INT64_MAX, minZ = INT64_MAX;
    int64_t maxX = INT64_MIN, maxY = INT64_MIN, maxZ = INT64_MIN;

    for (const auto& [regionName, region] : regions->children) {
        if (region.type != Compound) continue;
        const Vec3i pos = ReadVec(region.Get("Position"));
        const Vec3i size = ReadVec(region.Get("Size"));
        const Tag* palette = region.Get("BlockStatePalette");
        const Tag* states = region.Get("BlockStates");
        if (!palette || palette->type != List || palette->items.empty() || !states || states->type != LongArray)
            continue;
        const int64_t sx = std::llabs(size.x), sy = std::llabs(size.y), sz = std::llabs(size.z);
        const int64_t volume = sx * sy * sz;
        if (volume <= 0 || sx > 32000 || sy > 32000 || sz > 32000) continue;

        // Global block index for each palette entry (0xFFFF = air).
        std::vector<uint16_t> remap;
        for (const Tag& entry : palette->items) {
            const std::string id = entry.GetText("Name");
            if (IsAir(id) || id.empty()) {
                remap.push_back(0xFFFF);
                continue;
            }
            auto it = stateIndex.find(id);
            if (it == stateIndex.end()) {
                if (out.states.size() >= 0xFFFE) {
                    remap.push_back(0xFFFF);
                    continue;
                }
                it = stateIndex.emplace(id, static_cast<uint16_t>(out.states.size())).first;
                out.states.push_back(id);
            }
            remap.push_back(it->second);
        }

        // Litematica packs palette indexes tightly into longs; an index can span two longs.
        int bits = 2;
        while ((size_t{1} << bits) < palette->items.size()) ++bits;
        const auto& longs = states->longs;
        if (static_cast<uint64_t>(volume) * bits > longs.size() * 64ull) continue;
        const uint64_t mask = (1ull << bits) - 1;

        // A negative size means the region grows towards negative coordinates from its position.
        const int64_t baseX = pos.x + (size.x < 0 ? size.x + 1 : 0);
        const int64_t baseY = pos.y + (size.y < 0 ? size.y + 1 : 0);
        const int64_t baseZ = pos.z + (size.z < 0 ? size.z + 1 : 0);
        ++out.regions;

        for (int64_t index = 0; index < volume; ++index) {
            const uint64_t bit = static_cast<uint64_t>(index) * bits;
            const size_t word = bit >> 6, shift = bit & 63;
            uint64_t value = static_cast<uint64_t>(longs[word]) >> shift;
            if (shift + bits > 64) value |= static_cast<uint64_t>(longs[word + 1]) << (64 - shift);
            value &= mask;
            if (value >= remap.size() || remap[value] == 0xFFFF) continue;
            if (placed.size() >= kMaxBlocks) {
                error = "The schematic is too big (more than 4 million blocks).";
                return false;
            }
            const int64_t x = baseX + index % sx, z = baseZ + (index / sx) % sz, y = baseY + index / (sx * sz);
            placed.push_back({x, y, z, remap[value]});
            minX = std::min(minX, x), minY = std::min(minY, y), minZ = std::min(minZ, z);
            maxX = std::max(maxX, x), maxY = std::max(maxY, y), maxZ = std::max(maxZ, z);
        }
    }

    if (placed.empty()) {
        error = out.regions ? "The schematic only contains air." : "No readable regions in this file.";
        return false;
    }
    if (maxX - minX >= 32000 || maxY - minY >= 32000 || maxZ - minZ >= 32000) {
        error = "The schematic is too big.";
        return false;
    }
    out.sizeX = static_cast<int>(maxX - minX + 1);
    out.sizeY = static_cast<int>(maxY - minY + 1);
    out.sizeZ = static_cast<int>(maxZ - minZ + 1);
    out.blocks.reserve(placed.size());
    for (const Placed& p : placed) {
        out.blocks.push_back({static_cast<int16_t>(p.x - minX), static_cast<int16_t>(p.y - minY),
                              static_cast<int16_t>(p.z - minZ), p.state});
    }
    return true;
}
