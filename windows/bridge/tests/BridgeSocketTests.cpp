#include "BridgeTestSupport.h"
#include "bridge/AppLauncher.h"
#include "bridge/BridgeClient.h"
#include "bridge/BridgeServer.h"
#include "bridge/LocalPipe.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <atomic>
#include <thread>

using namespace wp;
using namespace wp::testing;

/// Replies `ok` with a summary of the request; never replies to `listImports`.
class TestHandler : public BridgeHandler
{
public:
    std::atomic<int> offMain{0};

    void handle(const BridgeRequest &request, std::function<void(BridgeResponse)> reply) override
    {
        if (QThread::currentThread() != QCoreApplication::instance()->thread())
            ++offMain;
        switch (request.kind) {
        case BridgeRequest::Kind::ping:
            reply(BridgeResponse::ok("pong"));
            break;
        case BridgeRequest::Kind::createNote: {
            const QString text = QStringLiteral("%1 %2").arg(request.title).arg(request.markdown ? request.markdown->size() : 0);
            std::thread([reply, text] { reply(BridgeResponse::ok(text)); }).detach();
            break;
        }
        case BridgeRequest::Kind::listImports:
            break;
        case BridgeRequest::Kind::addPage:
            reply(BridgeResponse::failure(QStringLiteral("no note '%1'").arg(*request.note)));
            reply(BridgeResponse::ok("second reply is ignored"));
            break;
        default:
            reply(BridgeResponse::ok(request.description()));
        }
    }
};

class BridgeSocketTests : public QObject
{
    Q_OBJECT

    static inline int counter = 0;
    QString name;
    TestHandler handler;
    BridgeServer *server = nullptr;

    BridgeClient client() const { return BridgeClient(name); }

    /// Writes raw bytes on one connection and reads `count` reply lines.
    QStringList exchange(const QString &bytes, int count) const
    {
        const QString pipe = name;
        return background([pipe, bytes, count] {
            QLocalSocket socket;
            socket.connectToServer(pipe);
            QStringList lines;
            if (!socket.waitForConnected(5000))
                return lines;
            socket.write(bytes.toUtf8());
            socket.waitForBytesWritten(5000);
            QByteArray received;
            while (received.count('\n') < count) {
                if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(5000))
                    break;
                received.append(socket.readAll());
            }
            for (const QByteArray &line : received.split('\n')) {
                if (!line.isEmpty())
                    lines.append(QString::fromUtf8(line));
            }
            return lines;
        });
    }

    static QString encoded(const BridgeRequest &request)
    {
        return QString::fromUtf8(QJsonDocument(request.toJson()).toJson(QJsonDocument::Compact));
    }

    static BridgeResponse decoded(const QString &line)
    {
        return BridgeResponse::fromJson(parseObject(line));
    }

    QList<BridgeResponse> decodedAll(const QStringList &lines) const
    {
        QList<BridgeResponse> result;
        for (const QString &line : lines)
            result.append(decoded(line));
        return result;
    }

private slots:
    void init()
    {
        name = QStringLiteral("wpb-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter);
        handler.offMain = 0;
        server = new BridgeServer(&handler, name);
    }

    void cleanup()
    {
        delete server;
        server = nullptr;
    }

    void roundTripCallsHandlerOnMainThread()
    {
        server->start();
        const BridgeClient c = client();
        const auto replies = background([c] {
            return QList<BridgeResponse>{c.send(BridgeRequest::ping()),
                                         c.send(BridgeRequest::createNote("T", QString("abc"))),
                                         c.send(BridgeRequest::readNote("n1", 2))};
        });
        QVERIFY((replies == QList<BridgeResponse>{BridgeResponse::ok("pong"), BridgeResponse::ok("T 3"),
                                                  BridgeResponse::ok(BridgeRequest::readNote("n1", 2).description())}));
        QCOMPARE(handler.offMain.load(), 0);
    }

    void wireFormatIsOneJSONObjectPerLine()
    {
        // Same bytes as Swift's synthesized Codable.
        QCOMPARE(encoded(BridgeRequest::ping()), QStringLiteral("{\"ping\":{}}"));
        QCOMPARE(encoded(BridgeRequest::readNote("n1", 2)), QStringLiteral("{\"readNote\":{\"note\":\"n1\",\"page\":2}}"));
        QCOMPARE(encoded(BridgeRequest::getPoints()), QStringLiteral("{\"getPoints\":{}}"));
        QCOMPARE(encoded(BridgeRequest::readChunk("i1", 3)),
                 QStringLiteral("{\"readChunk\":{\"chunk\":3,\"importID\":\"i1\"}}"));
        QCOMPARE(QString::fromUtf8(QJsonDocument(BridgeResponse::ok("hi").toJson()).toJson(QJsonDocument::Compact)),
                 QStringLiteral("{\"ok\":{\"_0\":\"hi\"}}"));

        const QString line = encoded(BridgeRequest::write("n1", 1, "a\nb", WriteMode::replace));
        QVERIFY(!line.contains('\n'));
        QVERIFY(BridgeRequest::fromJson(parseObject(line)) == BridgeRequest::write("n1", 1, "a\nb", WriteMode::replace));

        const StudyPlan plan("T", "O",
                             {StudyModule("M", 5, {StudyPoint("p", Importance::good, "r", QString("t"))})},
                             {StudyTask("do", QString("mon"))}, {"box a"});
        const BridgeRequest request = BridgeRequest::buildStudyPlan({"i1"}, plan);
        QVERIFY(BridgeRequest::fromJson(parseObject(encoded(request))) == request);
        const BridgeRequest cards = BridgeRequest::createFlashcards(std::nullopt, 2, "TCP", {Flashcard("Q", "A", QString("r"))});
        QVERIFY(BridgeRequest::fromJson(parseObject(encoded(cards))) == cards);
    }

    void largePayload()
    {
        server->start();
        const QString markdown = QString("Lorem ipsum dolor sit amet.\n").repeated(60000);
        QVERIFY(markdown.toUtf8().size() > 1500000);
        const BridgeClient c = client();
        const BridgeResponse reply = background([c, markdown] { return c.send(BridgeRequest::createNote("Big", markdown)); });
        QVERIFY(reply == BridgeResponse::ok(QStringLiteral("Big %1").arg(markdown.size())));
    }

    void concurrentClients()
    {
        server->start();
        const QString pipe = name;
        std::vector<std::future<std::optional<BridgeResponse>>> futures;
        for (int i = 0; i < 16; ++i) {
            futures.push_back(std::async(std::launch::async, [pipe, i]() -> std::optional<BridgeResponse> {
                try {
                    return BridgeClient(pipe).send(
                        BridgeRequest::createNote(QStringLiteral("c%1").arg(i), QString(i * 10000, QLatin1Char('x'))), 10);
                } catch (const BridgeError &) {
                    return std::nullopt;
                }
            }));
        }
        for (int i = 0; i < 16; ++i) {
            while (futures[i].wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
                QCoreApplication::processEvents();
            const auto reply = futures[i].get();
            QVERIFY2(reply.has_value(), qPrintable(QString::number(i)));
            QVERIFY(*reply == BridgeResponse::ok(QStringLiteral("c%1 %2").arg(i).arg(i * 10000)));
        }
    }

    void pipelinedRequestsAndMalformedLines()
    {
        server->start();
        const QString input = QStringLiteral("garbage\n%1\n\n{\"unknownCase\":{}}\n%2\n")
                                  .arg(encoded(BridgeRequest::ping()), encoded(BridgeRequest::createNote("A")));
        const QStringList lines = exchange(input, 4);
        QVERIFY((decodedAll(lines) == QList<BridgeResponse>{BridgeResponse::failure("malformed request"),
                                                            BridgeResponse::ok("pong"),
                                                            BridgeResponse::failure("malformed request"),
                                                            BridgeResponse::ok("A 0")}));
    }

    void secondReplyIsIgnored()
    {
        server->start();
        const QString input = QStringLiteral("%1\n%2\n").arg(encoded(BridgeRequest::addPage("n9")), encoded(BridgeRequest::ping()));
        const QStringList lines = exchange(input, 2);
        QVERIFY((decodedAll(lines) == QList<BridgeResponse>{BridgeResponse::failure("no note 'n9'"), BridgeResponse::ok("pong")}));
    }

    void clientTimesOut()
    {
        server->start();
        const BridgeClient c = client();
        QElapsedTimer timer;
        timer.start();
        const auto error = background([c] { return caught<BridgeError>([&] { c.send(BridgeRequest::listImports(), 0.5); }); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::timedOut(0.5));
        QVERIFY(timer.elapsed() < 3000);
    }

    void clientReportsAppNotRunning()
    {
        const auto error = caught<BridgeError>([&] { client().send(BridgeRequest::ping()); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::appNotRunning());
    }

    void pipeIsRemovedOnStop()
    {
        server->start();
        QVERIFY(server->isListening());
        server->stop();
        QVERIFY(!server->isListening());
        const auto error = caught<BridgeError>([&] { client().send(BridgeRequest::ping()); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::appNotRunning());
    }

    void serverCanRestartAfterStop()
    {
        server->start();
        server->stop();
        server->start();
        const BridgeClient c = client();
        const BridgeResponse reply = background([c] { return c.send(BridgeRequest::ping()); });
        QVERIFY(reply == BridgeResponse::ok("pong"));
    }

    void refusesToStealALivePipe()
    {
        server->start();
        BridgeServer second(&handler, name);
        const auto error = caught<BridgeError>([&] { second.start(); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::alreadyRunning(name));
        QTest::qWait(100); // lets the first server accept the probe connection
        QVERIFY(LocalPipe::isAccepting(name));
    }

    void startIsIdempotent()
    {
        server->start();
        server->start();
        QVERIFY(LocalPipe::isAccepting(name));
    }

    void longNamesAreRejected()
    {
        const QString longName = QString(300, QLatin1Char('x'));
        BridgeServer s(&handler, longName);
        auto error = caught<BridgeError>([&] { s.start(); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::pathTooLong(longName));
        error = caught<BridgeError>([&] { BridgeClient(longName).send(BridgeRequest::ping()); });
        QVERIFY(error.has_value());
        QVERIFY(*error == BridgeError::pathTooLong(longName));
    }

    void errorDescriptions()
    {
        QCOMPARE(BridgeError::appNotRunning().description(), QStringLiteral("Whiteprint isn't running"));
        QCOMPARE(BridgeError::timedOut(120).description(), QStringLiteral("Whiteprint didn't reply within 120 s"));
        QCOMPARE(BridgeError::system("bind", "Permission denied").description(),
                 QStringLiteral("bind failed: Permission denied"));
        QCOMPARE(BridgeError::launchFailed("x").description(), QStringLiteral("couldn't start Whiteprint: x"));
        QCOMPARE(BridgeError::badReply("y").description(), QStringLiteral("bad reply from Whiteprint: y"));
        QCOMPARE(BridgeError::alreadyRunning("p").description(), QStringLiteral("another Whiteprint is already listening on p"));
    }

    void socketNameOverride()
    {
        QCOMPARE(BridgePaths::socketEnvironmentKey(), QStringLiteral("WHITEPRINT_SOCKET"));
        const QByteArray saved = qgetenv("WHITEPRINT_SOCKET");
        qputenv("WHITEPRINT_SOCKET", "wpb-override");
        QCOMPARE(BridgePaths::socketName(), QStringLiteral("wpb-override"));
        qputenv("WHITEPRINT_SOCKET", "C:\\tmp\\wpb.sock");
        QCOMPARE(BridgePaths::socketName(), QStringLiteral("C__tmp_wpb.sock"));
        qputenv("WHITEPRINT_SOCKET", "\\\\.\\pipe\\wpb-full");
        QCOMPARE(BridgePaths::socketName(), QStringLiteral("wpb-full"));
        qunsetenv("WHITEPRINT_SOCKET");
        QVERIFY(BridgePaths::socketName().startsWith(QStringLiteral("whiteprint-")));
        if (!saved.isEmpty())
            qputenv("WHITEPRINT_SOCKET", saved);
    }

    void appExecutableIsFoundNextToHelper()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString helper = dir.filePath("whiteprint-mcp.exe");
        QVERIFY(AppLauncher::appExecutable(helper).isEmpty());
        QFile app(dir.filePath("Whiteprint.exe"));
        QVERIFY(app.open(QIODevice::WriteOnly));
        app.close();
        QCOMPARE(QDir::fromNativeSeparators(AppLauncher::appExecutable(helper)),
                 QDir::fromNativeSeparators(QFileInfo(app).absoluteFilePath()));
        QVERIFY(BridgePaths::helperExecutable().endsWith(QStringLiteral("whiteprint-mcp.exe")));
    }

    void ensureRunningReturnsWhenServerIsUp()
    {
        server->start();
        AppLauncher::ensureRunning(name, 1);
    }
};

QTEST_GUILESS_MAIN(BridgeSocketTests)
#include "BridgeSocketTests.moc"
