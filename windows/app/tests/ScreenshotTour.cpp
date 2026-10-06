// README screenshot tour (Windows twin of the macOS ScreenshotMode.swift).
// Skipped unless WHITEPRINT_SCREENSHOTS names an output folder; windows/make-screenshots.ps1 runs it and
// then shrinks the PNGs into docs/images/windows. It opens the real app windows on demo notes in a temporary
// notes folder, with their own defaults suite and bridge pipe, and saves PNGs of them (client area plus a
// drawn Windows caption and shadow, since the offscreen platform has no native frame).
//
//   WHITEPRINT_SCREENSHOTS=<dir>            where the PNGs go
#include "app/AppController.h"
#include "app/AppDefaults.h"
#include "app/AppServices.h"
#include "app/CommandPalette.h"
#include "app/FlashcardProgressStore.h"
#include "app/FlashcardStudyWindow.h"
#include "app/MacStyle.h"
#include "app/NoteDocuments.h"
#include "app/NoteWindow.h"
#include "app/SettingsPages.h"
#include "app/SettingsWindow.h"
#include "app/SidebarWidget.h"
#include "app/StudyPanel.h"
#include "app/StudySession.h"
#include "app/ViewPreferences.h"
#include "bridge/Bridge.h"
#include "ScreenshotNotes.h"
#include "core/DrawingCompiler.h"
#include "editor/BlockTextView.h"
#include "editor/DeckBlockView.h"
#include "editor/DrawingBlockView.h"
#include "editor/DrawingSourceEditor.h"
#include "editor/NoteEditorView.h"
#include "render/BlueprintBackground.h"
#include "render/Fonts.h"
#include "render/PDFExporter.h"
#include "render/SceneRenderer.h"
#include "study/StudyStore.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QStackedWidget>
#include <QStyle>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

using namespace wp;

namespace {

// MARK: Drawing a window's frame (the offscreen platform has none)

constexpr int shadowMargin = 36;
constexpr int captionHeight = 32;

/// `client` in a Windows 11 style window: rounded corners, caption with the title and the three caption
/// buttons, a soft shadow. `overlay` paints extra things (popovers, sheets) in client coordinates.
QImage windowShot(const QPixmap &client, const QString &title, bool dark,
                  const std::function<void(QPainter &)> &overlay = {})
{
    const double dpr = client.devicePixelRatio();
    const QSizeF c = QSizeF(client.size()) / dpr;
    const QSizeF total(c.width() + 2 * shadowMargin, c.height() + captionHeight + 2 * shadowMargin);
    QImage out((total * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF window(shadowMargin, shadowMargin, c.width(), c.height() + captionHeight);
    for (int i = 18; i >= 1; --i) { // a cheap blurred shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 5));
        p.drawRoundedRect(window.adjusted(-i, -i + 10, i, i + 10), 8 + i, 8 + i);
    }
    QPainterPath clip;
    clip.addRoundedRect(window, 8, 8);
    p.save();
    p.setClipPath(clip);
    const QColor bar = dark ? QColor(0x20, 0x20, 0x20) : QColor(0xf3, 0xf3, 0xf3);
    p.fillRect(QRectF(window.left(), window.top(), window.width(), captionHeight), bar);
    p.drawPixmap(QPointF(window.left(), window.top() + captionHeight), client);
    // Title and caption buttons.
    const QColor ink = dark ? QColor(255, 255, 255, 230) : QColor(0, 0, 0, 220);
    p.setPen(ink);
    QFont f = fonts::system(12);
    p.setFont(f);
    p.drawText(QRectF(window.left() + 12, window.top(), window.width() - 160, captionHeight), Qt::AlignVCenter | Qt::AlignLeft, title);
    p.setPen(QPen(ink, 1));
    const double y = window.top() + captionHeight / 2.0;
    const double right = window.right();
    p.drawLine(QPointF(right - 138, y), QPointF(right - 128, y));                       // minimize
    p.drawRect(QRectF(right - 93, y - 5, 10, 10));                                      // maximize
    p.drawLine(QPointF(right - 40, y - 5), QPointF(right - 30, y + 5));                 // close
    p.drawLine(QPointF(right - 40, y + 5), QPointF(right - 30, y - 5));
    if (overlay) {
        p.save();
        p.translate(window.left(), window.top() + captionHeight);
        overlay(p);
        p.restore();
    }
    p.restore();
    p.setPen(QPen(QColor(128, 128, 128, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(window.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    return out;
}

/// A widget's pixels, drawn at `pos` (global) relative to `origin`'s global top-left, for overlays.
void drawOverlay(QPainter &p, QWidget *base, QWidget *over)
{
    p.drawPixmap(QPointF(over->mapToGlobal(QPoint(0, 0)) - base->mapToGlobal(QPoint(0, 0))), over->grab());
}

/// Settings shows where this build lives and the user's name; show what an installed copy shows instead.
void tidyPaths(QWidget *root)
{
    const QString app = QCoreApplication::applicationDirPath();
    const QString home = QDir::homePath();
    auto fix = [&](QString text) {
        for (const QString &sep : {QString(QChar(0x2f)), QString(QDir::separator()), QString(2, QDir::separator())}) { // also JSON-escaped
            const auto conv = [&](const QString &path) { return QDir::fromNativeSeparators(path).replace(QLatin1Char('/'), sep); };
            text.replace(conv(app), conv(QStringLiteral("C:/Users/you/AppData/Local/Programs/Whiteprint")));
            text.replace(conv(home), conv(QStringLiteral("C:/Users/you")));
        }
        return text;
    };
    for (QLabel *l : root->findChildren<QLabel *>())
        if (l->text() != fix(l->text())) l->setText(fix(l->text()));
    for (QLineEdit *e : root->findChildren<QLineEdit *>())
        if (e->text() != fix(e->text())) e->setText(fix(e->text()));
    for (QPlainTextEdit *e : root->findChildren<QPlainTextEdit *>())
        if (e->toPlainText() != fix(e->toPlainText())) e->setPlainText(fix(e->toPlainText()));
}

void settle(int ms = 600)
{
    QApplication::processEvents();
    QTest::qWait(ms);
    QApplication::processEvents();
}

QWidget *topLevel(const std::function<bool(QWidget *)> &match)
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->isVisible() && match(w))
            return w;
    return nullptr;
}

} // namespace

class ScreenshotTour : public QObject {
    Q_OBJECT

    QString m_out;
    QDir m_notes;
    bool m_dark = false;

    void save(const QImage &image, const QString &name)
    {
        QVERIFY2(image.save(QDir(m_out).filePath(name + QStringLiteral(".png"))), qPrintable(name));
    }
    NoteWindow *openNote(const QString &relative)
    {
        NoteDocument *doc = NoteDocuments::instance().show(m_notes.filePath(relative));
        NoteWindow *w = AppController::instance().window(doc);
        w->resize(1280, 800);
        w->show();
        w->sidebar()->setExpandedFolders({QStringLiteral("Courses"), QStringLiteral("Courses/Networks")});
        settle(900);
        return w;
    }
    QImage shotOf(QWidget *w, const QString &title, const std::function<void(QPainter &)> &overlay = {})
    {
        return windowShot(w->grab(), title, m_dark, overlay);
    }
    void setDark(bool dark)
    {
        m_dark = dark;
        mac::setDarkOverride(dark ? 1 : 0);
        mac::apply(*qApp);
        settle(300);
    }

    // Images that need no window.

    void drawingLanguage()
    {
        const auto compiled = DrawingCompiler::compile(QString::fromUtf8(drawingExample));
        const QSizeF canvas = SceneRenderer::canvasSize(compiled.scene);
        const QSize size(1200, 420);
        const int split = 470;
        QImage img(size * 2, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(2);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        QPainterPath round;
        round.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 14, 14);
        p.setClipPath(round);
        p.fillRect(QRect(0, 0, split, size.height()), QColor(28, 33, 43));
        p.setPen(QColor(255, 255, 255, 140));
        p.setFont(fonts::system(13, QFont::DemiBold));
        p.drawText(QPointF(32, 40), QStringLiteral("Whiteprint drawing language"));
        // Source, with commands, strings, numbers and comments coloured.
        const QFont mono = fonts::monospaced(16);
        p.setFont(mono);
        const QFontMetrics fm(mono);
        const QRegularExpression token(QStringLiteral("(\"[^\"]*\")|(\\b\\d+,\\d+\\b)|(\\b(?:dashed|thick|bold)\\b)|(\\S+)|(\\s+)"));
        double y = 90;
        for (const QString &line : QString::fromUtf8(drawingExample).split(QLatin1Char('\n'))) {
            double x = 32;
            bool first = true;
            if (line.startsWith(QLatin1Char('#'))) {
                p.setPen(QColor(255, 255, 255, 105));
                p.drawText(QPointF(x, y), line);
            } else {
                for (auto it = token.globalMatch(line); it.hasNext();) {
                    const auto m = it.next();
                    const QString t = m.captured();
                    QColor c(235, 235, 235);
                    QFont f = mono;
                    if (m.hasCaptured(1)) c = QColor(153, 224, 158);
                    else if (m.hasCaptured(2) || m.hasCaptured(3)) c = QColor(250, 199, 120);
                    else if (m.hasCaptured(4) && first) { c = QColor(159, 211, 255); f.setWeight(QFont::DemiBold); }
                    if (m.hasCaptured(4) || m.hasCaptured(1)) first = false;
                    p.setFont(f);
                    p.setPen(c);
                    p.drawText(QPointF(x, y), t);
                    x += QFontMetricsF(f).horizontalAdvance(t);
                }
            }
            y += 34;
        }
        // The drawing, on a blueprint sheet.
        const QRectF sheet(split, 0, size.width() - split, size.height());
        BlueprintBackground::draw(p, sheet, BlueprintPalette::blueprint());
        const double scale = std::min({1.4, (sheet.width() - 80) / canvas.width(), (sheet.height() - 60) / canvas.height()});
        p.save();
        p.translate(sheet.center().x() - canvas.width() * scale / 2, sheet.center().y() - canvas.height() * scale / 2);
        p.scale(scale, scale);
        SceneRenderer::draw(compiled.scene, p, BlueprintPalette::blueprint());
        p.restore();
        p.end();
        save(img, QStringLiteral("drawing-language"));
    }

    void pdfExport()
    {
        const Note note = Note::parsing(readFile(m_notes.filePath(QStringLiteral("Courses/Networks/Lecture 3 – TCP.wprint"))));
        const QDateTime date(QDate(2026, 10, 5), QTime(9, 0));
        QList<QImage> pages;
        for (PDFExportStyle style : {PDFExportStyle::blueprint, PDFExportStyle::print}) {
            QByteArray pdf = PDFExporter::data(note, style, date);
            QBuffer buffer(&pdf);
            buffer.open(QIODevice::ReadOnly);
            QPdfDocument doc;
            doc.load(&buffer);
            const QSizeF pt = doc.pagePointSize(0);
            pages.push_back(doc.render(0, QSize(960, int(960 * pt.height() / pt.width()))));
        }
        const int gap = 40, inset = 30, pw = 480;
        const int ph = pw * pages[0].height() / pages[0].width();
        QImage img(QSize(inset * 2 + pw * 2 + gap, inset * 2 + ph) * 2, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(2);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        for (int i = 0; i < 2; ++i) {
            const QRect frame(inset + i * (pw + gap), inset, pw, ph);
            for (int s = 16; s >= 1; --s) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 7));
                p.drawRoundedRect(frame.adjusted(-s, -s + 8, s, s + 8), s, s);
            }
            p.fillRect(frame, Qt::white);
            p.drawImage(frame, pages[i]);
        }
        p.end();
        save(img, QStringLiteral("pdf-export"));
    }

    static QString readFile(const QString &path)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll());
    }

private slots:
    void tour()
    {
        m_out = qEnvironmentVariable("WHITEPRINT_SCREENSHOTS");
        if (m_out.isEmpty())
            QSKIP("WHITEPRINT_SCREENSHOTS not set");
        QDir().mkpath(m_out);
        m_notes = QDir(qEnvironmentVariable("WHITEPRINT_NOTES_DIR"));
        for (const DemoNote &n : demoNotes) {
            const QString path = m_notes.filePath(QString::fromUtf8(n.path));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(n.text));
        }
        qApp->setFont(fonts::system(13));
        AppDefaults::store().clear();
        ViewPreferences::shared().setLayoutMode(ViewLayout::slides);
        ViewPreferences::shared().setShowsMarkdownSyntax(false);
        setDark(false);
        AppController::instance().start();

        drawingLanguage();
        pdfExport();

        // The editor: slides, syntax hidden; A4; syntax shown; dark chrome.
        NoteWindow *mainWin = openNote(QStringLiteral("System design.wprint"));
        const QString mainTitle = QStringLiteral("System design – Whiteprint");
        save(shotOf(mainWin, mainTitle), QStringLiteral("main"));
        ViewPreferences::shared().setLayoutMode(ViewLayout::a4);
        settle(900);
        save(shotOf(mainWin, mainTitle), QStringLiteral("a4-layout"));
        ViewPreferences::shared().setLayoutMode(ViewLayout::slides);
        ViewPreferences::shared().setShowsMarkdownSyntax(true);
        settle(900);
        save(shotOf(mainWin, mainTitle), QStringLiteral("markdown-syntax"));
        ViewPreferences::shared().setShowsMarkdownSyntax(false);
        setDark(true);
        settle(600);
        save(shotOf(mainWin, mainTitle), QStringLiteral("main-dark"));
        setDark(false);
        settle(600);

        // Drawing popover: source and live preview.
        const auto blocks = mainWin->editor()->findChildren<DrawingBlockView *>();
        QVERIFY(!blocks.isEmpty());
        DrawingBlockView *block = blocks.first();
        mainWin->resize(1280, 1000); // room for the popover under the drawing
        settle(700);
        QTest::mouseDClick(block, Qt::LeftButton, {}, block->rect().center());
        settle(900);
        if (auto *pop = qobject_cast<DrawingSourceEditor *>(topLevel([](QWidget *w) { return qobject_cast<DrawingSourceEditor *>(w); }))) {
            save(shotOf(mainWin, mainTitle, [&](QPainter &p) { drawOverlay(p, mainWin, pop); }), QStringLiteral("drawing-editor"));
            pop->close();
            mainWin->resize(1280, 800);
            settle(500);
        } else {
            qWarning("no drawing popover");
        }

        // Flashcard deck on the lecture's second page, one answer revealed.
        NoteWindow *lecture = openNote(QStringLiteral("Courses/Networks/Lecture 3 – TCP.wprint"));
        for (int i = 0; i < 2; ++i) {
            lecture->scrollToPage(2);
            settle(800);
        }
        const auto decks = lecture->editor()->findChildren<DeckBlockView *>();
        if (!decks.isEmpty() && decks.first()->isVisible()) {
            QTest::mouseClick(decks.first(), Qt::LeftButton, {}, QPoint(decks.first()->width() / 2, 92));
            settle(500);
        }
        save(shotOf(lecture, QStringLiteral("Lecture 3 – TCP – Whiteprint")), QStringLiteral("flashcard-deck"));

        // Study window: question, then answer.
        {
            QTemporaryDir tmp;
            FlashcardProgressStore store(tmp.path());
            CardDeck deck(QStringLiteral("c1"), QStringLiteral("TCP basics"),
                          {Flashcard(QStringLiteral("What does TCP guarantee that IP does not?"),
                                     QStringLiteral("Reliable, in-order delivery of a byte stream, with lost segments retransmitted."),
                                     QStringLiteral("Lecture 3 – TCP.pdf · p. 4")),
                           Flashcard(QStringLiteral("Name the three segments of the handshake."), QStringLiteral("SYN, SYN-ACK, ACK."),
                                     QStringLiteral("Lecture 3 – TCP.pdf · p. 7"))});
            FlashcardStudyWindow cards(m_notes.filePath(QStringLiteral("Courses/Networks/Lecture 3 – TCP.wprint")),
                                       QStringLiteral("Lecture 3 – TCP"), deck, &store);
            cards.resize(720, 560);
            cards.show();
            settle(700);
            save(windowShot(cards.grab(), QStringLiteral("Study Flashcards"), m_dark), QStringLiteral("flashcards-question"));
            cards.flip();
            settle(500);
            save(windowShot(cards.grab(), QStringLiteral("Study Flashcards"), m_dark), QStringLiteral("flashcards-answer"));
            cards.close();
        }

        // Command palette over the System design note.
        mainWin->raise();
        mainWin->activateWindow();
        settle(400);
        AppController::instance().showCommandPalette(mainWin);
        settle(800);
        if (auto *palette = topLevel([](QWidget *w) { return qobject_cast<CommandPalette *>(w); }))
            save(shotOf(mainWin, mainTitle, [&](QPainter &p) { drawOverlay(p, mainWin, palette); }), QStringLiteral("command-palette"));
        else
            qWarning("no command palette");
        if (auto *palette = topLevel([](QWidget *w) { return qobject_cast<CommandPalette *>(w); }))
            palette->hide();

        // Study panel, over the note, with two imports and the first one's points saved.
        {
            QTemporaryDir tmp;
            StudyStore store(tmp.filePath(QStringLiteral("study")), [](const QString &path) {
                ExtractedDocument d;
                d.name = QFileInfo(path).fileName();
                for (int i = 1; i <= 12; ++i)
                    d.units.push_back({QStringLiteral("p. %1").arg(i), QStringLiteral("Content of page %1. ").arg(i).repeated(60)});
                return d;
            });
            for (const QString &name : {QStringLiteral("Lecture 3 – TCP.pdf"), QStringLiteral("Week 4 – Routing.docx")}) {
                QFile f(tmp.filePath(name));
                f.open(QIODevice::WriteOnly);
                f.write(name.toUtf8());
                f.close();
                const StudyImport imported = store.importFile(tmp.filePath(name));
                if (name.startsWith(QStringLiteral("Lecture")))
                    store.savePoints({StudyPoint(QStringLiteral("TCP gives a reliable, ordered byte stream on top of IP"), Importance::must, QStringLiteral("p. 1")),
                                      StudyPoint(QStringLiteral("Three-way handshake: SYN, SYN-ACK, ACK"), Importance::must, QStringLiteral("p. 1"))},
                                     imported.id, 1);
            }
            StudySession session(&store, nullptr);
            session.refreshImports();
            StudyPanel panel(nullptr, &session);
            panel.setFixedWidth(560);
            panel.adjustSize();
            panel.show();
            settle(500);
            save(windowShot(panel.grab(), QStringLiteral("Study Material"), m_dark), QStringLiteral("study-panel"));
            panel.close();
        }

        // The study plan note Claude wrote.
        NoteWindow *plan = openNote(QStringLiteral("Courses/Networks/Study plan – Networks midterm.wprint"));
        save(shotOf(plan, QStringLiteral("Networks midterm – Whiteprint")), QStringLiteral("study-plan"));

        // Settings, AI tab (the claude path is replaced by what an installed copy shows).
        {
            SettingsWindow settings;
            settings.setCurrentTab(0);
            // The offscreen screen is small, so the window caps and scrolls the page; show the whole page.
            auto *stack = settings.findChild<QStackedWidget *>();
            QWidget *aiPage = settings.page(0);
            stack->setFixedSize(aiPage->sizeHint().width() + 48, std::max(aiPage->sizeHint().height(), aiPage->layout()->totalHeightForWidth(aiPage->sizeHint().width())) + 24);
            settings.adjustSize();
            settings.show();
            settle(2500);
            static_cast<AISettingsPage *>(settings.page(0))->claudeFound(QStringLiteral("C:/Users/you/.local/bin/claude.exe"));
            tidyPaths(&settings);
            settle(500);
            settings.adjustSize();

            save(windowShot(settings.grab(), QStringLiteral("Settings"), m_dark), QStringLiteral("settings-ai"));
            settings.close();
        }

        // Slash menu, typed into the System design note (last: it edits the note).
        mainWin->raise();
        mainWin->activateWindow();
        settle(500);
        for (BlockTextView *view : mainWin->editor()->findChildren<BlockTextView *>()) {
            if (!view->toPlainText().contains(QStringLiteral("design review.")))
                continue;
            view->setFocus();
            QTextCursor c = view->textCursor();
            c.movePosition(QTextCursor::End);
            view->setTextCursor(c);
            QTest::keyClick(view, Qt::Key_Return);
            QTest::keyClick(view, Qt::Key_Slash);
            settle(800);
            save(shotOf(mainWin, mainTitle), QStringLiteral("slash-menu"));
            break;
        }

        // Leave no unsaved edit behind (the slash menu typed into a temp note) and no stored preferences.
        for (NoteWindow *w : AppController::instance().windows())
            w->close();
        AppController::instance().shutdown();
        AppDefaults::store().clear();
        const QString data = BridgePaths::supportDirectory();
        if (data.contains(QStringLiteral(".qttest")))
            QDir(data).removeRecursively();
        mac::setDarkOverride(-1);
    }
};

int main(int argc, char **argv)
{
    // Everything the tour touches is private to it: temp notes, own defaults suite and bridge pipe.
    static QTemporaryDir notes;
    static QTemporaryDir roaming; // %APPDATA%: study imports and the Claude Desktop config are looked up there
    if (!qEnvironmentVariableIsEmpty("WHITEPRINT_SCREENSHOTS")) {
        qputenv("WHITEPRINT_NOTES_DIR", notes.path().toUtf8());
        qputenv("WHITEPRINT_DEFAULTS_SUITE", "screenshots");
        qputenv("APPDATA", roaming.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true); // app data (study imports, flashcard progress) goes to ~/.qttest
        qputenv("WHITEPRINT_SOCKET", QByteArray("wp-shots-") + QByteArray::number(qint64(QCoreApplication::applicationPid())));
        if (!qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", "1.25");
    }
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QApplication app(argc, argv);
    ScreenshotTour tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ScreenshotTour.moc"
