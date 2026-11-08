#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Cry3DEngine's chunk file. Only the chunk descriptor handed to a loader
     * is modeled.
     */
    struct IChunkFile {
        /**
         * One chunk of a loaded file, as `CInternalDatabaseInfo::ReadController905`
         * (0x180766210) reads it: the data at +0x10, its size at +0x18, the
         * endian flag at +0x1C.
         */
        struct ChunkDesc {
            std::uint32_t chunkType;    // 0x00
            std::uint32_t chunkVersion; // 0x04
            std::int32_t chunkId;       // 0x08
            std::int32_t fileOffset;    // 0x0C
            void *data;                 // 0x10
            std::int32_t size;          // 0x18
            bool bSwapEndian;           // 0x1C

          private:
            static void AssertLayout() {
                static_assert(offsetof(ChunkDesc, data) == 0x10, "IChunkFile::ChunkDesc::data offset mismatch");
                static_assert(offsetof(ChunkDesc, size) == 0x18, "IChunkFile::ChunkDesc::size offset mismatch");
                static_assert(offsetof(ChunkDesc, bSwapEndian) == 0x1C, "IChunkFile::ChunkDesc::bSwapEndian offset mismatch");
            }
        };
    };
} // namespace SDK
