#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// One non-air block, relative to the schematic's lowest corner.
struct SchematicBlock {
    int16_t x, y, z;
    uint16_t state; // index into Schematic::states
};

struct Schematic {
    std::string name;   // from the file's metadata, or the file name
    std::string author;
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    std::vector<std::string> states; // block ids without properties, e.g. "minecraft:oak_planks"
    std::vector<SchematicBlock> blocks;
    size_t regions = 0;
};

// Reads a Litematica .litematic file (gzip-compressed NBT). Returns false and fills `error` on failure.
bool LoadLitematic(const std::filesystem::path& path, Schematic& out, std::string& error);

// Decompresses gzip data (or a raw deflate stream). Exposed for tests.
bool Gunzip(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
