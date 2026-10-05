#include "extract/Extraction.h"

#include <QtTest>
#include <algorithm>

using namespace wp;

class ChunkerTests : public QObject {
    Q_OBJECT

    static ExtractedDocument document(const std::vector<std::pair<QString, QString>> &units) {
        ExtractedDocument doc;
        doc.name = QStringLiteral("doc.pdf");
        for (const auto &u : units) doc.units.push_back({u.first, u.second});
        return doc;
    }
    static QStringList refs(const std::vector<ExtractedChunk> &chunks, bool first) {
        QStringList out;
        for (const auto &c : chunks) out << (first ? c.firstRef : c.lastRef);
        return out;
    }

private slots:
    void emptyDocumentHasNoChunks() { QVERIFY(Chunker::chunks(document({})).empty()); }

    void smallDocumentIsOneChunkWithRefMarkers() {
        auto chunks = Chunker::chunks(document({{"p. 1", "One"}, {"p. 2", "Two\nlines"}}));
        QCOMPARE(chunks.size(), size_t(1));
        QVERIFY((chunks[0] == ExtractedChunk{1, "p. 1", "p. 2", "[p. 1]\nOne\n\n[p. 2]\nTwo\nlines"}));
    }

    void groupsUnitsWithoutSplittingThem() {
        // Each block is "[s N]\n" (6) + 20 characters = 26; two blocks with the separator are 54.
        std::vector<std::pair<QString, QString>> units;
        for (int i = 1; i <= 5; ++i) units.push_back({QStringLiteral("s %1").arg(i), QString(20, QChar('0' + i))});
        auto chunks = Chunker::chunks(document(units), 54);
        QCOMPARE(chunks.size(), size_t(3));
        QCOMPARE(chunks[0].index, 1);
        QCOMPARE(chunks[2].index, 3);
        QCOMPARE(refs(chunks, true), (QStringList{"s 1", "s 3", "s 5"}));
        QCOMPARE(refs(chunks, false), (QStringList{"s 2", "s 4", "s 5"}));
        QVERIFY(std::all_of(chunks.begin(), chunks.end(), [](const auto &c) { return c.text.size() <= 54; }));
        QCOMPARE(Chunker::chunks(document(units), 53).size(), size_t(5));
    }

    void oversizedUnitSplitsAtParagraphsKeepingItsRef() {
        QStringList paragraphs;
        for (int i = 1; i <= 6; ++i) paragraphs << QString(30, QChar('0' + i));
        auto chunks = Chunker::chunks(document({{"p. 1", "x"}, {"p. 2", paragraphs.join("\n\n")}, {"p. 3", "y"}}), 80);
        for (const auto &c : chunks) QVERIFY2(c.text.size() <= 80, qPrintable(QString::number(c.text.size())));
        int p2Blocks = 0;
        QString all;
        for (const auto &c : chunks) {
            all += c.text;
            for (const QString &block : c.text.split("\n\n[")) if (block.contains("p. 2]")) ++p2Blocks;
        }
        QCOMPARE(p2Blocks, 3);
        QVERIFY(all.contains("[p. 2]\n" + paragraphs[0] + "\n\n" + paragraphs[1]));
        QCOMPARE(chunks.front().firstRef, QString("p. 1"));
        QCOMPARE(chunks.back().lastRef, QString("p. 3"));
        for (const QString &paragraph : paragraphs) {
            QVERIFY2(std::any_of(chunks.begin(), chunks.end(), [&](const auto &c) { return c.text.contains(paragraph); }), "paragraph lost");
        }
    }

    void oversizedParagraphSplitsAtLines() {
        QStringList lines;
        for (int i = 1; i <= 10; ++i) lines << QStringLiteral("line number %1").arg(i);
        auto chunks = Chunker::chunks(document({{"slide 1", lines.join("\n")}}), 50);
        QVERIFY(chunks.size() > 1);
        QStringList recovered;
        for (const auto &c : chunks) {
            QVERIFY(c.text.size() <= 50 && c.text.startsWith("[slide 1]\n"));
            recovered << c.text.split('\n').mid(1);
        }
        QCOMPARE(recovered, lines);
    }

    void oneHugeLineIsStillSplit() {
        QString text;
        for (int i = 0; i < 100; ++i) text += "word ";
        text += QString(120, 'z');
        auto chunks = Chunker::chunks(document({{"p. 9", text}}), 60);
        int zs = 0;
        for (const auto &c : chunks) {
            QVERIFY(c.text.size() <= 60);
            zs += int(c.text.mid(QString("[p. 9]\n").size()).count('z'));
        }
        QCOMPARE(zs, 120);
    }

    void defaultLimitKeepsTypicalDeckInFewChunks() {
        std::vector<std::pair<QString, QString>> units;
        QString content;
        for (int i = 0; i < 60; ++i) content += "content ";
        for (int i = 1; i <= 100; ++i) units.push_back({QStringLiteral("slide %1").arg(i), content});
        auto chunks = Chunker::chunks(document(units));
        QCOMPARE(chunks.size(), size_t(3));
        for (const auto &c : chunks) QVERIFY(c.text.size() <= Chunker::defaultMaxCharacters);
        QCOMPARE(chunks[0].index, 1);
        QCOMPARE(chunks[1].index, 2);
        QCOMPARE(chunks[2].index, 3);
    }
};

QTEST_APPLESS_MAIN(ChunkerTests)
#include "ChunkerTests.moc"
