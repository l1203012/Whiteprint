#include "Fixtures.h"
#include "extract/PresentationExtractor.h"
#include "extract/ZipArchive.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

class ZipArchiveTests : public QObject {
    Q_OBJECT

    QTemporaryDir directory;

    static void expectFailure(const std::function<void()> &body, ZipArchive::Failure::Kind kind, const QString &detail = {}, bool checkDetail = false) {
        bool threw = false;
        try {
            body();
        } catch (const ZipArchive::Failure &f) {
            threw = true;
            QCOMPARE(int(f.kind()), int(kind));
            if (checkDetail) QCOMPARE(f.detail(), detail);
        }
        QVERIFY2(threw, "expected a ZipArchive::Failure");
    }

private slots:
    void initTestCase() { QVERIFY(directory.isValid()); }

    void readsDeflatedAndStoredEntries() {
        QString lon;
        for (int i = 0; i < 200; ++i) lon += "Repetitive text compresses well. ";
        const QString path = directory.filePath("mixed.zip");
        QVERIFY(Fixtures::writeZip({{"a/long.txt", lon}, {"b/plain.txt", "stored as is"}, {"empty.txt", ""}}, {"b/plain.txt"}, path));

        ZipArchive archive(path);
        QCOMPARE(int(archive.entry("a/long.txt")->method), 8);
        QCOMPARE(int(archive.entry("b/plain.txt")->method), 0);
        QCOMPARE(*archive.contents("a/long.txt"), lon.toUtf8());
        QCOMPARE(*archive.contents("b/plain.txt"), QByteArray("stored as is"));
        QCOMPARE(*archive.contents("empty.txt"), QByteArray());
        QVERIFY(!archive.contents("missing.txt").has_value());
    }

    void handBuiltStoredArchive() {
        ZipArchive archive(Fixtures::storedZip("x.xml", "<x/>"));
        QCOMPARE(archive.entries().size(), size_t(1));
        QCOMPARE(archive.entries()[0].name, QString("x.xml"));
        QCOMPARE(*archive.contents("x.xml"), QByteArray("<x/>"));
    }

    void rejectsDataThatIsNotAZip() {
        expectFailure([] { ZipArchive(QByteArray("not a zip at all, just some text")); }, ZipArchive::Failure::Kind::NotAZip);
        bool threw = false;
        try { ZipArchive(QByteArray{}); } catch (const ZipArchive::Failure &) { threw = true; }
        QVERIFY(threw);
    }

    void rejectsTruncatedArchive() {
        const QByteArray data = Fixtures::storedZip("x.xml", QByteArray(100, 'x'));
        // Dropping the start keeps the end record but its offsets now point past the data.
        bool threw = false;
        try { ZipArchive(data.mid(60)).contents("x.xml"); } catch (const ZipArchive::Failure &) { threw = true; }
        QVERIFY(threw);
    }

    void rejectsAbsurdUncompressedSize() {
        QByteArray data = Fixtures::storedZip("bomb.xml", "tiny");
        const int directoryOffset = int(data.size()) - 22 - (46 + 8);
        // Uncompressed size in the central directory: 3 GB.
        data.replace(directoryOffset + 24, 4, QByteArray("\x00\x00\x00\xC0", 4));
        ZipArchive archive(data);
        expectFailure([&] { archive.contents("bomb.xml"); }, ZipArchive::Failure::Kind::TooLarge, "bomb.xml", true);
    }

    void detectsChecksumMismatch() {
        QByteArray data = Fixtures::storedZip("x.txt", "hello");
        data[30 + 5] = 'j';
        // (30 byte local header + 5 byte name = start of the data; "hello" becomes "jello" wait: name is 5 bytes)
        ZipArchive archive(data);
        expectFailure([&] { archive.contents("x.txt"); }, ZipArchive::Failure::Kind::Corrupt, "checksum mismatch for x.txt", true);
    }

    void detectsCorruptDeflateStream() {
        const QString path = directory.filePath("deflated.zip");
        QString text;
        for (int i = 0; i < 500; ++i) text += "abcdefghij";
        QVERIFY(Fixtures::writeZip({{"t.txt", text}}, {}, path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        f.close();
        const int start = 30 + 5;
        for (int i = start; i < start + 8; ++i) data[i] = char(data[i] ^ 0xFF);
        bool threw = false;
        try { ZipArchive(data).contents("t.txt"); } catch (const ZipArchive::Failure &) { threw = true; }
        QVERIFY(threw);
    }

    void inflatesDynamicHuffmanBlocksOfRealisticText() {
        // Pseudo-random words give dynamic Huffman blocks with many distinct symbols and long back-references.
        QByteArray text;
        quint32 seed = 12345;
        for (int i = 0; i < 20000; ++i) {
            seed = seed * 1664525u + 1013904223u;
            text += char('a' + (seed >> 24) % 26);
            if ((seed >> 20) % 7 == 0) text += ' ';
        }
        text += text.left(5000);
        const QString path = directory.filePath("big.zip");
        QVERIFY(Fixtures::writeFile(path, Fixtures::makeZip({{"big.txt", text}})));
        ZipArchive archive(path);
        QCOMPARE(int(archive.entry("big.txt")->method), 8);
        QCOMPARE(*archive.contents("big.txt"), text);
    }

    void crc32MatchesKnownValue() { QCOMPARE(CRC32::checksum("123456789"), uint32_t(0xCBF43926)); }

    void resolvesPackagePaths() {
        QCOMPARE(PackagePath::resolve("../notesSlides/notesSlide3.xml", "ppt/slides/slide3.xml"), QString("ppt/notesSlides/notesSlide3.xml"));
        QCOMPARE(PackagePath::resolve("slides/slide1.xml", "ppt/presentation.xml"), QString("ppt/slides/slide1.xml"));
        QCOMPARE(PackagePath::resolve("/ppt/slides/./slide2.xml", "ppt/presentation.xml"), QString("ppt/slides/slide2.xml"));
    }
};

QTEST_APPLESS_MAIN(ZipArchiveTests)
#include "ZipArchiveTests.moc"
