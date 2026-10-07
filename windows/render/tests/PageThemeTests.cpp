// Port of PageThemeTests (MarkdownStylerTests.swift).
#include "render/Palette.h"

#include <QSet>
#include <QtTest>

using namespace wp;

class PageThemeTests : public QObject {
    Q_OBJECT

private slots:
    void everyThemeHasItsOwnPalette()
    {
        QCOMPARE(PageThemes::defaultTheme, PageTheme::paper);
        QCOMPARE(PageThemes::palette(PageTheme::blueprint).pageBackground, BlueprintPalette::blueprint().pageBackground);
        QVERIFY(PageThemes::palette(PageTheme::blueprint).showsGrid);
        QVERIFY(PageThemes::palette(PageTheme::blueprint).drawsSheet);
        QSet<QString> titles;
        for (PageTheme theme : PageThemes::all()) {
            titles.insert(PageThemes::title(theme));
            QVERIFY(PageThemes::fromRawValue(PageThemes::rawValue(theme)) == theme);
            if (theme == PageTheme::blueprint)
                continue;
            const BlueprintPalette palette = PageThemes::palette(theme);
            QVERIFY2(!palette.showsGrid, qPrintable(PageThemes::title(theme) + " pages are plain"));
            QVERIFY(!palette.drawsSheet);
            QCOMPARE(palette.canvas, palette.pageBackground);
        }
        QCOMPARE(titles.size(), PageThemes::all().size());
        QVERIFY(!PageThemes::fromRawValue(QStringLiteral("plaid")));
    }

    void paperAdaptsToDarkMode()
    {
        const BlueprintPalette light = BlueprintPalette::paper(false);
        const BlueprintPalette dark = BlueprintPalette::paper(true);
        QCOMPARE(light.pageBackground, QColor(0xFC, 0xFB, 0xF9));
        QCOMPARE(light.text, QColor(0x1D, 0x1D, 0x1F));
        QCOMPARE(light.accent, QColor(0x0A, 0x66, 0xD8));
        QCOMPARE(dark.pageBackground, QColor(0x1E, 0x1E, 0x20));
        QCOMPARE(dark.text, QColor(0xEC, 0xEC, 0xEE));
        QCOMPARE(dark.accent, QColor(0x4C, 0x9A, 0xFF));
        QVERIFY(PageThemes::palette(PageTheme::paper, true) == dark);
        // The other themes look the same in light and dark mode.
        QVERIFY(PageThemes::palette(PageTheme::sepia, true) == PageThemes::palette(PageTheme::sepia, false));
    }

    void exportPalettesKeepTheirSheet()
    {
        for (const BlueprintPalette &palette : {BlueprintPalette::blueprint(), BlueprintPalette::print()}) {
            QVERIFY(palette.drawsSheet);
            QVERIFY(palette.showsGrid);
            QVERIFY(!palette.canvas.isValid());
            QCOMPARE(palette.checkbox, palette.accent);
        }
    }
};

QTEST_GUILESS_MAIN(PageThemeTests)
#include "PageThemeTests.moc"
