#include "core/tests/TestSupport.h"
#include "study/StudyStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <memory>
#include <mutex>
#include <thread>

using namespace wp;
using namespace wp::testing;

class StudyStoreTests : public QObject
{
    Q_OBJECT

    QTemporaryDir root;

    QString storePath() const { return root.filePath("Study"); }

    /// Fake extractor: every line of the file is `ref|text`.
    static ExtractedDocument extract(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            throw ExtractionError::unreadable(QFileInfo(path).fileName());
        ExtractedDocument doc;
        doc.name = QFileInfo(path).fileName();
        for (const QString &line : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const qsizetype bar = line.indexOf('|');
            doc.units.push_back({line.left(bar), line.mid(bar + 1)});
        }
        return doc;
    }

    /// Fake chunker: two units per chunk.
    static std::vector<ExtractedChunk> chunk(const ExtractedDocument &document)
    {
        std::vector<ExtractedChunk> chunks;
        for (size_t start = 0; start < document.units.size(); start += 2) {
            const size_t end = std::min(start + 2, document.units.size());
            QStringList parts;
            for (size_t i = start; i < end; ++i)
                parts << "[" + document.units[i].ref + "]\n" + document.units[i].text;
            chunks.push_back({int(start / 2 + 1), document.units[start].ref, document.units[end - 1].ref, parts.join('\n')});
        }
        return chunks;
    }

    std::unique_ptr<StudyStore> makeStore() const
    {
        return std::make_unique<StudyStore>(storePath(), &StudyStoreTests::extract, &StudyStoreTests::chunk);
    }

    QString file(const QString &name, int units, const QString &salt = {}) const
    {
        const QString path = root.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("can't write %s", qPrintable(path));
        QStringList lines;
        for (int i = 1; i <= units; ++i)
            lines << QString("slide %1|Content %1%2").arg(i).arg(salt);
        f.write(lines.join('\n').toUtf8());
        return path;
    }

    static StudyPoint point(const QString &text, Importance importance = Importance::must, const QString &ref = "slide 1",
                            std::optional<QString> topic = std::nullopt)
    {
        return StudyPoint(text, importance, ref, topic);
    }

    static QStringList texts(const QList<StudyPoint> &points)
    {
        QStringList t;
        for (const auto &p : points)
            t << p.text;
        return t;
    }

    static QStringList ids(const QList<StudyImport> &imports)
    {
        QStringList t;
        for (const auto &i : imports)
            t << i.id;
        return t;
    }

private slots:
    void init() { QVERIFY(root.isValid()); QDir(storePath()).removeRecursively(); }

    void importAssignsIncreasingIDs()
    {
        auto store = makeStore();
        const StudyImport a = store->importFile(file("A.pptx", 5));
        const StudyImport b = store->importFile(file("B.pdf", 2));
        QCOMPARE(a.id, QString("i1"));
        QCOMPARE(a.name, QString("A.pptx"));
        QCOMPARE(a.unitCount, 5);
        QCOMPARE(a.chunkCount, 3);
        QCOMPARE(a.chunksDone, 0);
        QCOMPARE(b.id, QString("i2"));
        QCOMPARE(ids(store->imports()), QStringList({"i1", "i2"}));
    }

    void sameContentReturnsExistingImport()
    {
        auto store = makeStore();
        const StudyImport a = store->importFile(file("A.pptx", 3));
        const StudyImport copy = store->importFile(file("Copy of A.pptx", 3));
        const StudyImport other = store->importFile(file("A2.pptx", 3, "!"));
        WP_EQ(copy, a);
        QCOMPARE(other.id, QString("i2"));
        QCOMPARE(store->imports().size(), qsizetype(2));
    }

    void emptyDocumentIsRejected()
    {
        StudyStore store(storePath(), &StudyStoreTests::extract, [](const ExtractedDocument &) { return std::vector<ExtractedChunk>(); });
        const auto error = caught<ExtractionError>([&] { store.importFile(file("A.pdf", 1)); });
        QVERIFY(error && *error == ExtractionError::empty("A.pdf"));
        QVERIFY(store.imports().isEmpty());
    }

    void extractionErrorsPassThrough()
    {
        StudyStore store(storePath(), [](const QString &p) -> ExtractedDocument {
            throw ExtractionError::unsupportedType(QFileInfo(p).fileName());
        });
        auto error = caught<ExtractionError>([&] { store.importFile(file("A.key", 1)); });
        QVERIFY(error && *error == ExtractionError::unsupportedType("A.key"));
        error = caught<ExtractionError>([&] { store.importFile(root.filePath("missing.pdf")); });
        QVERIFY(error && *error == ExtractionError::unreadable("missing.pdf"));
    }

    void chunkTextHasHeader()
    {
        auto store = makeStore();
        store->importFile(file("Lecture3.pptx", 3));
        QCOMPARE(store->chunk("i1", 1), U("Lecture3.pptx \xc2\xb7 chunk 1/2 \xc2\xb7 slide 1\xe2\x80\x93slide 2\n\n[slide 1]\nContent 1\n[slide 2]\nContent 2"));
        QCOMPARE(store->chunk("i1", 2), U("Lecture3.pptx \xc2\xb7 chunk 2/2 \xc2\xb7 slide 3\n\n[slide 3]\nContent 3"));
    }

    void chunkAndImportAreChecked()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 3));
        auto error = caught<StudyError>([&] { store->chunk("i1", 0); });
        QVERIFY(error && *error == StudyError::chunkOutOfRange(0, 2));
        QVERIFY(caught<StudyError>([&] { store->chunk("i1", 3); }));
        error = caught<StudyError>([&] { store->chunk("i9", 1); });
        QVERIFY(error && *error == StudyError::unknownImport("i9"));
        QVERIFY(caught<StudyError>([&] { store->savePoints({}, "i1", 5); }));
        QVERIFY(caught<StudyError>([&] { store->savePoints({}, "i9", 1); }));
        QVERIFY(caught<StudyError>([&] { store->points(QString("i9")); }));
    }

    void savePointsReplacesAndCountsChunks()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 6));
        store->savePoints({point("Old")}, "i1", 2);
        store->savePoints({point("New"), point("Detail", Importance::good, "slide 4")}, "i1", 2);
        store->savePoints({}, "i1", 1);
        QCOMPARE(store->imports()[0].chunksDone, 2);
        QCOMPARE(texts(store->points(QString("i1"))), QStringList({"New", "Detail"}));
    }

    void pointsAreTidiedAndRefsNamed()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 2));
        store->savePoints({point("  Two\nlines ", Importance::must, "p. 3", QString(" ")), point("   "),
                           point("Named", Importance::must, U("A.pdf \xc2\xb7 p. 4"), QString("Graphs")),
                           point("No ref", Importance::must, "")},
                          "i1", 1);
        const QList<StudyPoint> expected = {
            StudyPoint("Two lines", Importance::must, U("A.pdf \xc2\xb7 p. 3")),
            StudyPoint("Named", Importance::must, U("A.pdf \xc2\xb7 p. 4"), QString("Graphs")),
            StudyPoint("No ref", Importance::must, "A.pdf"),
        };
        WP_EQ(store->points(), expected);
    }

    void pointsFollowDocumentOrder()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 6));
        store->importFile(file("B.pdf", 2, "b"));
        store->savePoints({point("B1")}, "i2", 1);
        store->savePoints({point("A3")}, "i1", 3);
        store->savePoints({point("A1")}, "i1", 1);
        QCOMPARE(texts(store->points()), QStringList({"A1", "A3", "B1"}));
        QCOMPARE(texts(store->points(QString("i2"))), QStringList({"B1"}));
    }

    void summaryFormat()
    {
        auto store = makeStore();
        store->importFile(file("Lecture3.pptx", 8));
        store->importFile(file("Syllabus.pdf", 1, "s"));
        store->savePoints({point("TCP is connection-oriented", Importance::must, "slide 2", QString("transport")),
                           point("History of ARPANET", Importance::skip, "slide 1")},
                          "i1", 1);
        store->savePoints({point("Window size", Importance::good, "slide 7")}, "i1", 4);
        QCOMPARE(store->pointsSummary(),
                 U("i1 Lecture3.pptx \xc2\xb7 chunks 2/4 \xc2\xb7 not done: 2\xe2\x80\x93" "3\n"
                   "\xe2\x98\x85 TCP is connection-oriented (Lecture3.pptx \xc2\xb7 slide 2) [transport]\n"
                   "\xe2\x9c\x95 History of ARPANET (Lecture3.pptx \xc2\xb7 slide 1)\n"
                   "\xe2\x97\x8b Window size (Lecture3.pptx \xc2\xb7 slide 7)\n\n"
                   "i2 Syllabus.pdf \xc2\xb7 chunks 0/1 \xc2\xb7 not done: 1\n"
                   "(no points)"));
        store->savePoints({point("Essay due 2026-01-15", Importance::must, "slide 1", QString("task"))}, "i2", 1);
        QCOMPARE(store->pointsSummary(QString("i2")),
                 U("i2 Syllabus.pdf \xc2\xb7 chunks 1/1\n"
                   "\xe2\x98\x85 Essay due 2026-01-15 (Syllabus.pdf \xc2\xb7 slide 1) [task]"));
    }

    void summaryWithoutImports()
    {
        QCOMPARE(makeStore()->pointsSummary(), QString("no imports"));
    }

    void rangesAreCompact()
    {
        QCOMPARE(StudyStore::ranges({1, 2, 3, 5, 7, 8}), U("1\xe2\x80\x93" "3, 5, 7\xe2\x80\x93" "8"));
        QCOMPARE(StudyStore::ranges({4}), QString("4"));
    }

    void reopeningKeepsEverything()
    {
        StudyImport a;
        {
            auto store = makeStore();
            a = store->importFile(file("A.pdf", 4));
            store->savePoints({point("Kept", Importance::must, "slide 1", QString("x"))}, "i1", 2);
        }
        auto reopened = makeStore();
        QCOMPARE(reopened->imports().size(), qsizetype(1));
        QCOMPARE(reopened->imports()[0].id, a.id);
        QCOMPARE(reopened->imports()[0].chunksDone, 1);
        QVERIFY(qAbs(reopened->imports()[0].importedAt.toMSecsSinceEpoch() - a.importedAt.toMSecsSinceEpoch()) <= 1);
        QCOMPARE(texts(reopened->points(QString("i1"))), QStringList({"Kept"}));
        QVERIFY(reopened->chunk("i1", 2).endsWith("[slide 4]\nContent 4"));
        QCOMPARE(reopened->importFile(file("A.pdf", 4)).id, QString("i1"));
    }

    void indexUsesTheSwiftFileFormat()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 2));
        QFile f(storePath() + "/index.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject index = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(index["lastID"].toInt(), 1);
        const QJsonObject entry = index["entries"].toArray()[0].toObject();
        QCOMPARE(entry["hash"].toString().size(), 64);
        QCOMPARE(entry["info"].toObject()["unitCount"].toInt(), 2);
        // Dates are seconds since 2001-01-01, like Swift's JSONEncoder.
        QVERIFY(entry["info"].toObject()["importedAt"].toDouble() > 7e8);
        QVERIFY(QFileInfo::exists(storePath() + "/i1/chunks/1.json"));
    }

    void removeNeverReusesIDs()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 2));
        store->importFile(file("B.pdf", 2, "b"));
        store->remove("i1");
        QCOMPARE(ids(store->imports()), QStringList({"i2"}));
        QVERIFY(!QFileInfo::exists(storePath() + "/i1"));
        QVERIFY(caught<StudyError>([&] { store->remove("i1"); }));

        store->removeAll();
        QVERIFY(store->imports().isEmpty());
        QVERIFY(!QFileInfo::exists(storePath() + "/i2"));
        // Removed content can be imported again, under a fresh id.
        QCOMPARE(store->importFile(file("A.pdf", 2)).id, QString("i3"));
        QCOMPARE(ids(makeStore()->imports()), QStringList({"i3"}));
    }

    void corruptIndexIsAnError()
    {
        QDir().mkpath(storePath());
        QFile f(storePath() + "/index.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{nope");
        f.close();
        const auto error = caught<StudyError>([&] { makeStore(); });
        QVERIFY(error && *error == StudyError::corrupt("index.json"));
    }

    void corruptChunkAndPointsAreErrors()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 4));
        store->savePoints({point("x")}, "i1", 1);
        for (const auto &[path, content] : {std::pair<QString, QByteArray>{"/i1/chunks/2.json", "garbage"},
                                            {"/i1/points/1.json", "[{]"}}) {
            QFile f(storePath() + path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
        }
        auto error = caught<StudyError>([&] { store->chunk("i1", 2); });
        QVERIFY(error && *error == StudyError::corrupt("i1/chunks/2.json"));
        error = caught<StudyError>([&] { store->points(); });
        QVERIFY(error && *error == StudyError::corrupt("i1/points/1.json"));
        QVERIFY(caught<StudyError>([&] { store->pointsSummary(QString("i1")); }));
        store->chunk("i1", 1);
    }

    void concurrentAccess()
    {
        auto store = makeStore();
        store->importFile(file("A.pdf", 40));
        // Files are written up front: the helper is not thread-safe, the store is.
        for (int i = 0; i < 60; ++i) {
            if (i % 3 == 2)
                file(QString("C%1.pdf").arg(i), 2, QString::number(i));
        }
        std::mutex failures;
        int errors = 0;
        std::vector<std::thread> threads;
        for (int t = 0; t < 6; ++t) {
            threads.emplace_back([&, t] {
                for (int i = t; i < 60; i += 6) {
                    try {
                        const int n = i % 20 + 1;
                        switch (i % 3) {
                        case 0: store->savePoints({point(QString("P%1").arg(n)), point(QString("Q%1").arg(n), Importance::good)}, "i1", n); break;
                        case 1: store->chunk("i1", n); break;
                        default:
                            store->pointsSummary();
                            store->importFile(root.filePath(QString("C%1.pdf").arg(i)));
                        }
                    } catch (...) {
                        std::lock_guard lock(failures);
                        ++errors;
                    }
                }
            });
        }
        for (auto &t : threads)
            t.join();
        QCOMPARE(errors, 0);
        QSet<int> saved;
        for (int i = 0; i < 60; i += 3)
            saved.insert(i % 20 + 1);
        QCOMPARE(store->imports()[0].chunksDone, int(saved.size()));
        QCOMPARE(store->points(QString("i1")).size(), qsizetype(saved.size() * 2));
        QCOMPARE(store->imports().size(), qsizetype(21));
        QCOMPARE(makeStore()->imports().size(), qsizetype(21));
    }
};

QTEST_APPLESS_MAIN(StudyStoreTests)
#include "StudyStoreTests.moc"
