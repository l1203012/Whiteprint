// Renders the palette, settings, flashcard and study windows offscreen to PNG files for visual review.
// Skipped unless WP_SCREENSHOT_DIR names an output folder. Light and dark are both rendered.
#include "app/AISettings.h"
#include "app/CommandPalette.h"
#include "app/FlashcardProgressStore.h"
#include "app/FlashcardStudyWindow.h"
#include "app/MacStyle.h"
#include "app/SettingsPages.h"
#include "app/SettingsWindow.h"
#include "app/StudyPanel.h"
#include "app/StudySession.h"
#include "render/Fonts.h"
#include "study/StudyStore.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

namespace {

void shoot(QWidget &w, const QString &dir, const QString &name, int dark)
{
    w.ensurePolished();
    if (!w.property("keepSize").toBool())
        w.adjustSize();
    w.show();
    QApplication::processEvents();
    w.grab().save(QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(name, dark ? "dark" : "light")));
}

QList<CommandPalette::Item> items()
{
    auto note = [](const QString &t, const QString &d) { return CommandPalette::Item{CommandPalette::Kind::note, t, d, {}}; };
    auto page = [](const QString &t, const QString &d) { return CommandPalette::Item{CommandPalette::Kind::page, t, d, {}}; };
    auto cmd = [](const QString &t, const QString &d) { return CommandPalette::Item{CommandPalette::Kind::command, t, d, {}}; };
    return {note("Networks lecture notes", "Courses/Networks"), note("Shopping list", "Note"), page("TCP basics", "Page 1"),
            page("Congestion control", "Page 2"), cmd("New note", "Ctrl+N"), cmd("New folder", "Ctrl+Shift+N"),
            cmd("Study flashcards\u2026", "Study"), cmd("Settings", "Ctrl+,"), cmd("Export as PDF (Blueprint)\u2026", "Export")};
}

} // namespace

class UiScreenshots : public QObject {
    Q_OBJECT
private slots:
    void render()
    {
        const QString out = qEnvironmentVariable("WP_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("WP_SCREENSHOT_DIR not set");
        QDir().mkpath(out);
        QTemporaryDir tmp;
        qputenv("WHITEPRINT_NOTES_DIR", tmp.filePath("notes").toUtf8());
        qputenv("WHITEPRINT_DEFAULTS_SUITE", "shots");
        qApp->setFont(fonts::system(13));
        for (const int dark : {0, 1}) {
            mac::setDarkOverride(dark);
            mac::apply(*qApp);

            CommandPalette palette;
            palette.setItems(items(), "Search notes, pages and commands");
            shoot(palette, out, "palette", dark);
            palette.setQuery("not");
            shoot(palette, out, "palette-query", dark);

            QSettings defaults(tmp.filePath("s.ini"), QSettings::IniFormat);
            AISettings settings(defaults, "io.github.whiteprint.tests.shots");
            AISettingsPage ai(settings);
            ai.claudeFound(QString("C:/Users/me/.local/bin/claude.exe"));
            shoot(ai, out, "settings-ai", dark);

            for (int tab = 0; tab < 3; ++tab) {
                SettingsWindow settingsWindow;
                settingsWindow.setCurrentTab(tab);
                shoot(settingsWindow, out, QStringLiteral("settings-window%1").arg(tab), dark);
            }

            FlashcardProgressStore store(tmp.path() + "/cards" + QString::number(dark));
            CardDeck deck("c1", QString("TCP basics"),
                          {Flashcard("What does TCP guarantee?", "Ordered, reliable delivery of a byte stream.",
                                     QString("Lecture3.pptx \u00b7 slide 4")),
                           Flashcard("What is the three-way handshake?", "SYN, SYN-ACK, ACK.")});
            FlashcardStudyWindow cards("C:/notes/net.wprint", "Networks", deck, &store);
            cards.resize(680, 560);
            cards.setProperty("keepSize", true);
            shoot(cards, out, "cards-question", dark);
            cards.flip();
            shoot(cards, out, "cards-answer", dark);
            cards.rate(CardRating::good);
            cards.flip();
            cards.rate(CardRating::hard);
            shoot(cards, out, "cards-finished", dark);

            StudyStore studyStore(tmp.path() + "/study" + QString::number(dark), [](const QString &path) {
                ExtractedDocument d;
                d.name = QFileInfo(path).fileName();
                for (int i = 1; i <= 12; ++i)
                    d.units.push_back({QStringLiteral("slide %1").arg(i), QStringLiteral("Content of slide %1. ").repeated(40)});
                return d;
            });
            for (const QString &name : {"Lecture3.pptx", "Operating Systems - Chapter 7 (Memory management).pdf"}) {
                QFile f(tmp.filePath(name));
                f.open(QIODevice::WriteOnly);
                f.write(name.toUtf8());
                f.close();
                studyStore.importFile(tmp.filePath(name));
            }
            StudySession session(&studyStore, nullptr);
            session.refreshImports();
            StudyPanel panel(nullptr, &session);
            panel.setFixedWidth(560);
            shoot(panel, out, "study-panel", dark);
        }
        mac::setDarkOverride(-1);
    }
};

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QApplication app(argc, argv);
    UiScreenshots tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "UiScreenshots.moc"
