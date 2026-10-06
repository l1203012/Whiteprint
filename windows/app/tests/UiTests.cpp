// Tests for the logic behind the palette, settings, flashcard and study windows (the widgets run
// offscreen). There is no Swift counterpart: AppKit windows were not unit tested.
#include "app/AISettings.h"
#include "app/CommandPalette.h"
#include "app/FlashcardProgressStore.h"
#include "app/FlashcardStudyWindow.h"
#include "app/SettingsPages.h"
#include "app/StudyPanel.h"
#include "app/StudySession.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

namespace {

QList<CommandPalette::Item> sampleItems(QStringList *ran = nullptr)
{
    auto item = [ran](const QString &title, CommandPalette::Kind kind) {
        return CommandPalette::Item{kind, title, {}, [ran, title] {
                                        if (ran)
                                            *ran << title;
                                    }};
    };
    return {item("Alpha note", CommandPalette::Kind::note), item("Beta note", CommandPalette::Kind::note),
            item("New note", CommandPalette::Kind::command), item("Settings", CommandPalette::Kind::command)};
}

CardDeck deck()
{
    return CardDeck("c1", QString("TCP"), {Flashcard("Q1", "A1", QString("Lecture 3 \u00b7 slide 4")), Flashcard("Q2", "A2")});
}

} // namespace

class UiTests : public QObject {
    Q_OBJECT

private slots:
    // MARK: Palette

    void paletteListsEverythingForAnEmptyQuery()
    {
        CommandPalette palette;
        palette.setItems(sampleItems(), "Search");
        QCOMPARE(palette.shownTitles(), (QStringList{"Alpha note", "Beta note", "New note", "Settings"}));
        QCOMPARE(palette.selectedRow(), 0);
    }

    void paletteFiltersAndRanksByMatch()
    {
        CommandPalette palette;
        palette.setItems(sampleItems(), "Search");
        palette.setQuery("note");
        // A word start ("note" in "New note") ranks like the others; ties keep their order.
        QCOMPARE(palette.shownTitles().size(), 3);
        palette.setQuery("set");
        QCOMPARE(palette.shownTitles(), (QStringList{"Settings"}));
        palette.setQuery("zzz");
        QVERIFY(palette.shownTitles().isEmpty());
        palette.setQuery("");
        QCOMPARE(palette.shownTitles().size(), 4);
        QCOMPARE(palette.selectedRow(), 0);
    }

    void paletteKeyboardNavigationClampsAtTheEnds()
    {
        CommandPalette palette;
        palette.setItems(sampleItems(), "Search");
        auto *field = palette.findChild<QLineEdit *>();
        QVERIFY(field);
        QTest::keyClick(field, Qt::Key_Down);
        QCOMPARE(palette.selectedRow(), 1);
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(field, Qt::Key_Down);
        QCOMPARE(palette.selectedRow(), 3);
        QTest::keyClick(field, Qt::Key_Up);
        QCOMPARE(palette.selectedRow(), 2);
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(field, Qt::Key_Up);
        QCOMPARE(palette.selectedRow(), 0);
    }

    void paletteEnterRunsTheSelectionAfterClosing()
    {
        QStringList ran;
        CommandPalette palette;
        palette.setItems(sampleItems(&ran), "Search");
        palette.show();
        auto *field = palette.findChild<QLineEdit *>();
        QTest::keyClicks(field, "sett");
        QCOMPARE(palette.shownTitles(), (QStringList{"Settings"}));
        QTest::keyClick(field, Qt::Key_Return);
        QVERIFY(!palette.isVisible());
        QVERIFY(ran.isEmpty()); // runs once the panel is gone
        QTRY_COMPARE(ran, (QStringList{"Settings"}));
    }

    void paletteEscapeClosesWithoutRunning()
    {
        QStringList ran;
        CommandPalette palette;
        palette.setItems(sampleItems(&ran), "Search");
        palette.show();
        QTest::keyClick(palette.findChild<QLineEdit *>(), Qt::Key_Escape);
        QVERIFY(!palette.isVisible());
        QTest::qWait(30);
        QVERIFY(ran.isEmpty());
    }

    void paletteWithoutMatchesRunsNothing()
    {
        QStringList ran;
        CommandPalette palette;
        palette.setItems(sampleItems(&ran), "Search");
        palette.setQuery("zzz");
        palette.runSelected();
        QTest::qWait(30);
        QVERIFY(ran.isEmpty());
    }

    // MARK: Settings

    void aiPagePersistsProviderAndModel()
    {
        QTemporaryDir dir;
        QSettings defaults(dir.filePath("s.ini"), QSettings::IniFormat);
        AISettings settings(defaults, "io.github.whiteprint.tests.ui");
        AISettingsPage page(settings);
        QCOMPARE(page.providerPopup()->currentIndex(), 0);
        QSignalSpy spy(&page, &AISettingsPage::studyStateChanged);
        page.providerChanged(1);
        QCOMPARE(settings.provider(), AIProvider::grok);
        QCOMPARE(spy.count(), 1);

        page.modelField()->setText("  my-model  ");
        page.modelChanged();
        QCOMPARE(settings.grokModel(), QString("my-model"));
        QCOMPARE(page.modelField()->text(), QString("my-model"));
        page.modelField()->setText("   ");
        page.modelChanged();
        QCOMPARE(settings.grokModel(), GrokRunner::Configuration::defaultModel());

        // A page built later shows what was stored.
        AISettingsPage again(settings);
        QCOMPARE(again.providerPopup()->currentIndex(), 1);
    }

    void aiPageSavesAndRemovesTheKey()
    {
        QTemporaryDir dir;
        QSettings defaults(dir.filePath("s.ini"), QSettings::IniFormat);
        AISettings settings(defaults, "io.github.whiteprint.tests.ui." + QString::number(QCoreApplication::applicationPid()));
        settings.apiKeyItem.remove();
        AISettingsPage page(settings);
        QCOMPARE(page.keyStatus()->text(), QString("No key saved."));
        QVERIFY(!page.testButton()->isEnabled());

        QVERIFY(!page.saveKey()); // empty field: nothing to save
        page.apiKeyField()->setText("  xai-secret  ");
        QVERIFY(page.saveKey());
        QCOMPARE(settings.apiKeyItem.read().value_or(QString()), QString("xai-secret"));
        QVERIFY(page.apiKeyField()->text().isEmpty());
        QVERIFY(page.keyStatus()->text().startsWith(QString::fromUtf8("\u2713")));
        QVERIFY(page.testButton()->isEnabled());
        QVERIFY(settings.grokConfiguration().has_value());

        page.removeKey();
        QVERIFY(!settings.apiKeyItem.read().has_value());
        QCOMPARE(page.keyStatus()->text(), QString("No key saved."));
        QVERIFY(!page.testButton()->isEnabled());
    }

    void aiPageEnablesAddButtonOnlyWithClaude()
    {
        QTemporaryDir dir;
        QSettings defaults(dir.filePath("s.ini"), QSettings::IniFormat);
        AISettings settings(defaults, "io.github.whiteprint.tests.ui");
        AISettingsPage page(settings);
        page.claudeFound(std::nullopt);
        QVERIFY(!page.addButton()->isEnabled());
        QVERIFY(page.codeStatus()->text().startsWith("Not found"));
        page.claudeFound(QString("C:/tools/claude.exe"));
        QVERIFY(page.addButton()->isEnabled());
        QVERIFY(page.codeStatus()->text().contains("claude.exe"));
        QVERIFY(page.commandLabel()->text().contains("mcp add"));
    }

    // MARK: Flashcards

    void flashcardWindowFlipRateAndFinish()
    {
        QTemporaryDir dir;
        FlashcardProgressStore store(dir.path());
        FlashcardStudyWindow window("C:/notes/a.wprint", "Networks", deck(), &store);
        QCOMPARE(window.windowTitle(), QString("Study Flashcards"));
        QVERIFY(window.summaryText().contains("2 new"));
        QCOMPARE(window.cardView()->content().kind, FlashcardView::Content::Kind::card);
        QVERIFY(!window.cardView()->content().flipped);

        // Rating before the flip does nothing.
        QVERIFY(window.handleKey(Qt::Key_1, {}));
        QCOMPARE(window.session().stats().answers, 0);

        QVERIFY(window.handleKey(Qt::Key_Space, {}));
        QVERIFY(window.cardView()->content().flipped);
        QVERIFY(window.handleKey(Qt::Key_3, {})); // Good
        QCOMPARE(window.session().stats().answers, 1);
        QCOMPARE(window.cardView()->content().card.question, QString("Q2"));
        QCOMPARE(store.progress("C:/notes/a.wprint", "c1").size(), 1);

        window.flip();
        window.rate(CardRating::again); // goes to the back of the queue
        QCOMPARE(window.session().queue().size(), 1);
        window.flip();
        window.rate(CardRating::easy);
        QVERIFY(window.session().isFinished());
        QCOMPARE(window.cardView()->content().kind, FlashcardView::Content::Kind::finished);
        QCOMPARE(window.cardView()->content().stats.answers, 3);
        QCOMPARE(window.cardView()->content().stats.cards, 2);
        QCOMPARE(store.progress("C:/notes/a.wprint", "c1").size(), 2);
    }

    void flashcardWindowNothingDueThenStudyAll()
    {
        QTemporaryDir dir;
        FlashcardProgressStore store(dir.path());
        {
            FlashcardStudyWindow first("C:/notes/a.wprint", "Networks", deck(), &store);
            for (int i = 0; i < 2; ++i) {
                first.flip();
                first.rate(CardRating::easy);
            }
            QVERIFY(first.session().isFinished());
        }
        FlashcardStudyWindow window("C:/notes/a.wprint", "Networks", deck(), &store);
        QCOMPARE(window.cardView()->content().kind, FlashcardView::Content::Kind::message);
        QVERIFY(!window.handleKey(Qt::Key_Space, {})); // no card to flip
        window.studyAll();
        QCOMPARE(window.session().queue().size(), 2);
        QCOMPARE(window.cardView()->content().kind, FlashcardView::Content::Kind::card);
    }

    void flashcardWindowIgnoresShortcutsAndEscapeCloses()
    {
        QTemporaryDir dir;
        FlashcardProgressStore store(dir.path());
        FlashcardStudyWindow window("C:/notes/a.wprint", "Networks", deck(), &store);
        window.show();
        QVERIFY(!window.handleKey(Qt::Key_Space, Qt::ControlModifier));
        QVERIFY(!window.cardView()->content().flipped);
        QVERIFY(window.handleKey(Qt::Key_Escape, {}));
        QVERIFY(!window.isVisible());
    }

    void flashcardShuffleDoesNotLoseCards()
    {
        QTemporaryDir dir;
        FlashcardProgressStore store(dir.path());
        FlashcardStudyWindow window("C:/notes/a.wprint", "Networks", deck(), &store);
        window.setShuffled(true);
        QCOMPARE(window.session().queue().size(), 2);
    }

    // MARK: Study panel

    void studyPanelDisablesGenerateWithoutMaterial()
    {
        StudySession session(nullptr, nullptr);
        StudyPanel panel(nullptr, &session);
        QVERIFY(!panel.generateButton()->isEnabled());
        QVERIFY(!panel.cancelButton()->isVisibleTo(&panel));
        QCOMPARE(panel.importList()->count(), 1); // "No material yet."
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsSet("QT_QPA_PLATFORM") ? qgetenv("QT_QPA_PLATFORM") : QByteArray("offscreen"));
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QApplication app(argc, argv);
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "UiTests.moc"
