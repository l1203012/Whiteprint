#pragma once
// Port of Chunking.swift.
#include "extract/Extraction.h"

namespace wp {

/// A `[ref]` block: one unit, or one piece of a unit too big for a chunk.
struct ChunkBlock {
    QString ref;
    QString text;
    int length = 0;

    ChunkBlock(const QString &ref, const QString &body);

    /// Splits `unit` into blocks that each fit in `limit` characters, breaking at paragraphs, then lines,
    /// then spaces, and only mid-word as a last resort.
    static std::vector<ChunkBlock> blocks(const ExtractedUnit &unit, int limit);
};

} // namespace wp
