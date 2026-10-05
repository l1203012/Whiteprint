#ifndef Q_MOC_RUN
#include "StreamSamples.h"
#endif
#include "core/tests/TestSupport.h"
#include "study/ClaudeStreamParser.h"

using namespace wp;
using namespace wp::testing;
using Event = RunnerEvent;

Q_DECLARE_METATYPE(QList<RunnerEvent>)

class ClaudeStreamParserTests : public QObject
{
    Q_OBJECT

    StudyImport lecture{"i1", "Lecture3.pptx", 24, 12, 0, QDateTime::currentDateTimeUtc()};

    ImportInfo infoFor(bool known) const
    {
        const StudyImport l = lecture;
        return [l, known](const QString &id) -> std::optional<StudyImport> {
            return known && id == l.id ? std::optional<StudyImport>(l) : std::nullopt;
        };
    }

    QList<Event> parse(const QStringList &lines, bool known = false) const
    {
        ClaudeStreamParser parser(infoFor(known));
        return parser.feed((lines.join('\n') + "\n").toUtf8()) + parser.finish();
    }

private slots:
    void recordedRun()
    {
        const QList<Event> expected = {
            Event::status("Starting Claude…"),
            Event::text("I'll start by listing your imports."),
            Event::status("Looking at your imports…"),
            Event::status("Reading chunk 3 of 12 · Lecture3.pptx"),
            Event::status("Saving points…"),
            Event::status("Reading the drawing reference…"),
            Event::status("Merging the points…"),
            Event::status("Building the study plan…"),
            Event::text("Study plan ready: 4 modules, 3 tasks."),
            Event::finished(),
        };
        WP_EQ(parse(StreamSamples::run(), true), expected);
    }

    void unknownImportStillReportsProgress()
    {
        WP_EQ(parse({StreamSamples::run()[3]}), QList<Event>{Event::status("Reading chunk 3 · i1")});
    }

    void bytesSplitAnywhere()
    {
        const QByteArray bytes = (StreamSamples::run().join('\n') + "\n").toUtf8();
        ClaudeStreamParser parser(infoFor(true));
        QList<Event> events;
        qsizetype offset = 0;
        for (qsizetype size : {1, 7, 300, 2, 4096, 13}) {
            if (offset >= bytes.size())
                break;
            events += parser.feed(bytes.mid(offset, size));
            offset += size;
        }
        if (offset < bytes.size())
            events += parser.feed(bytes.mid(offset));
        WP_EQ(events, parse(StreamSamples::run(), true));
        QVERIFY(parser.isFinished());
    }

    void trailingLineWithoutNewline()
    {
        ClaudeStreamParser parser;
        WP_EQ(parser.feed(StreamSamples::run().last().toUtf8()), QList<Event>{});
        WP_EQ(parser.finish(), QList<Event>{Event::finished()});
    }

    void errorResults()
    {
        WP_EQ(parse({StreamSamples::notLoggedIn}), QList<Event>{Event::failed("Invalid API key · Please run /login")});
        WP_EQ(parse({StreamSamples::maxTurns}),
              QList<Event>{Event::failed("Claude stopped after too many steps. Run it again to continue.")});
        WP_EQ(parse({R"({"type":"result","subtype":"error_during_execution","is_error":true})"}),
              QList<Event>{Event::failed("Claude Code ran into an error (error_during_execution).")});
        WP_EQ(parse({R"({"type":"result","subtype":"error_max_budget_usd","is_error":true})"}),
              QList<Event>{Event::failed("Claude stopped at its spending limit.")});
        WP_EQ(parse({R"({"type":"result","subtype":"error_during_execution","is_error":true,"errors":["boom"]})"}),
              QList<Event>{Event::failed("boom")});
    }

    void helperThatFailedToStartEndsTheRun()
    {
        QStringList lines{StreamSamples::helperFailed};
        lines += StreamSamples::run();
        WP_EQ(parse(lines), QList<Event>{Event::failed("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint.")});
    }

    void noiseIsIgnored()
    {
        WP_EQ(parse({"",
                     "Warning: something on stdout",
                     R"({"type":"stream_event","event":{}})",
                     "[1,2]",
                     R"({"type":"assistant","message":{"content":[{"type":"thinking","thinking":"hmm"},{"type":"text","text":"  "}]}})"}),
              QList<Event>{});
    }

    void nothingAfterTheResult()
    {
        WP_EQ(parse({StreamSamples::run().last(), StreamSamples::run()[1]}), QList<Event>{Event::finished()});
    }

    void toolStatusLines()
    {
        const ImportInfo none;
        QCOMPARE(ToolStatus::text("mcp__whiteprint__create_flashcards", {}, none), QString("Making flashcards…"));
        QCOMPARE(ToolStatus::text("Bash", {}, none), QString("Working…"));
        QCOMPARE(ToolStatus::text("read_chunk", {}, none), QString("Reading…"));
        QCOMPARE(ToolStatus::text("read_chunk", QJsonObject{{"importID", "i2"}, {"chunk", 4}}, none), QString("Reading chunk 4 · i2"));
    }
};

QTEST_APPLESS_MAIN(ClaudeStreamParserTests)
#include "ClaudeStreamParserTests.moc"
