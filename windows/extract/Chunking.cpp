#include "extract/Chunking.h"

#include <algorithm>

namespace wp {

namespace {

QStringList pack(const QString &text, int budget, const QStringList &separators, int level) {
    if (text.size() <= budget) return {text};
    if (level >= separators.size()) {
        QStringList out;
        qsizetype start = 0;
        while (start < text.size()) {
            qsizetype end = std::min<qsizetype>(start + budget, text.size());
            // Never cut a surrogate pair in half.
            if (end < text.size() && end - start > 1 && text[end - 1].isHighSurrogate()) --end;
            out << text.mid(start, end - start);
            start = end;
        }
        return out;
    }
    const QString &separator = separators[level];
    QStringList pieces;
    QString current;
    int length = 0;
    for (const QString &part : text.split(separator, Qt::KeepEmptyParts)) {
        if (part.isEmpty()) continue;
        for (const QString &piece : pack(part, budget, separators, level + 1)) {
            int count = int(piece.size());
            if (length > 0 && length + separator.size() + count <= budget) {
                current += separator + piece;
                length += int(separator.size()) + count;
            } else {
                if (length > 0) pieces << current;
                current = piece;
                length = count;
            }
        }
    }
    if (length > 0) pieces << current;
    return pieces;
}

} // namespace

ChunkBlock::ChunkBlock(const QString &r, const QString &body)
    : ref(r), text(QStringLiteral("[%1]\n%2").arg(r, body)), length(int(text.size())) {}

std::vector<ChunkBlock> ChunkBlock::blocks(const ExtractedUnit &unit, int limit) {
    ChunkBlock whole(unit.ref, unit.text);
    if (whole.length <= limit) return {whole};
    int budget = std::max<int>(1, limit - int(QStringLiteral("[%1]\n").arg(unit.ref).size()));
    std::vector<ChunkBlock> out;
    for (const QString &piece : pack(unit.text, budget, {QStringLiteral("\n\n"), QStringLiteral("\n"), QStringLiteral(" ")}, 0))
        out.emplace_back(unit.ref, piece);
    return out;
}

std::vector<ExtractedChunk> Chunker::chunks(const ExtractedDocument &document, int maxCharacters) {
    std::vector<ChunkBlock> blocks;
    for (const ExtractedUnit &unit : document.units)
        for (auto &b : ChunkBlock::blocks(unit, maxCharacters)) blocks.push_back(std::move(b));

    std::vector<ExtractedChunk> chunks;
    std::vector<ChunkBlock> group;
    int length = 0;

    auto flush = [&] {
        if (group.empty()) return;
        QStringList texts;
        for (const auto &b : group) texts << b.text;
        chunks.push_back({int(chunks.size()) + 1, group.front().ref, group.back().ref, texts.join(QStringLiteral("\n\n"))});
        group.clear();
        length = 0;
    };

    for (const ChunkBlock &block : blocks) {
        int added = group.empty() ? block.length : length + 2 + block.length;
        if (!group.empty() && added > maxCharacters) flush();
        length = group.empty() ? block.length : length + 2 + block.length;
        group.push_back(block);
    }
    flush();
    return chunks;
}

} // namespace wp
