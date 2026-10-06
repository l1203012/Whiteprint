#include "editor/MarkdownEditing.h"

#include <QtTest>

using namespace wp;
namespace ME = wp::MarkdownEditing;

namespace {

/// Applies `change` and renders the caret as `|` (selections as `[...]`).
std::optional<QString> render(const QString &text, const std::optional<TextChange> &change)
{
    if (!change)
        return std::nullopt;
    QString result = change->applied(text);
    const TextRange s = change->selection;
    if (s.length == 0) {
        result.replace(s.location, 0, "|");
        return result;
    }
    const QString selected = result.mid(s.location, s.length);
    result.replace(s.location, s.length, "[" + selected + "]");
    return result;
}

/// `text` with `|` marking the caret.
std::pair<QString, TextRange> caret(QString marked)
{
    const int location = int(marked.indexOf('|'));
    marked.remove('|');
    return {marked, TextRange(location, 0)};
}

std::optional<QString> newline(const QString &marked)
{
    const auto [text, selection] = caret(marked);
    return render(text, ME::newline(text, selection));
}

std::optional<QString> indent(const QString &marked, bool outdent = false)
{
    const auto [text, selection] = caret(marked);
    return render(text, ME::indent(text, selection, outdent));
}

ME::SlashEffect slash(SlashCommand command, const QString &marked, const QString &typed)
{
    const auto [text, selection] = caret(marked);
    const int length = int(typed.size());
    return ME::apply(command, text, TextRange(selection.location - length, length));
}

std::optional<QString> slashText(SlashCommand command, const QString &marked, const QString &typed)
{
    const auto [text, selection] = caret(marked);
    Q_UNUSED(selection);
    const auto effect = slash(command, marked, typed);
    if (effect.kind != ME::SlashEffect::Kind::text)
        return std::nullopt;
    return render(text, effect.change);
}

} // namespace

// QCOMPARE on optionals of QString needs a printable form.
#define EQ(actual, expected) QCOMPARE((actual).value_or(QStringLiteral("<nil>")), QString(expected))
#define NIL(actual) QVERIFY(!(actual).has_value())

class MarkdownEditingTests : public QObject
{
    Q_OBJECT

private slots:
    // MARK: Line parsing

    void parsesLinePrefixes()
    {
        QVERIFY(MarkdownLinePrefix("## Title").kind == LineKind::heading(2));
        QCOMPARE(MarkdownLinePrefix("## Title").prefixLength, 3);
        QVERIFY(MarkdownLinePrefix("#hashtag").kind == LineKind::paragraph());
        QVERIFY(MarkdownLinePrefix("  - item").kind == LineKind::bullet('-'));
        QCOMPARE(MarkdownLinePrefix("  - item").indent, QString("  "));
        QCOMPARE(MarkdownLinePrefix("  - item").prefixLength, 4);
        QVERIFY(MarkdownLinePrefix("12) x").kind == LineKind::ordered(12, ')'));
        QVERIFY(MarkdownLinePrefix("- [ ] task").kind == LineKind::checklist(false, '-'));
        QVERIFY(MarkdownLinePrefix("* [X] done").kind == LineKind::checklist(true, '*'));
        QCOMPARE(MarkdownLinePrefix("- [ ]").prefixLength, 5);
        QVERIFY(MarkdownLinePrefix("- [ ]").checkboxRange() == TextRange(2, 3));
        QVERIFY(MarkdownLinePrefix("> quote").kind == LineKind::quote());
        QVERIFY(MarkdownLinePrefix("-not a list").kind == LineKind::paragraph());
        QVERIFY(MarkdownLinePrefix("2024. year").kind == LineKind::ordered(2024, '.'));
    }

    void textLines()
    {
        const QString text = "a\n\nbc\n";
        const auto lines = TextLine::all(text);
        QStringList texts;
        QList<int> starts;
        for (const auto &l : lines) {
            texts << l.text;
            starts << l.start();
        }
        QCOMPARE(texts, (QStringList{"a", "", "bc", ""}));
        QCOMPARE(starts, (QList<int>{0, 2, 3, 6}));
        QVERIFY(TextLine::at(6, text).range == TextRange(6, 0));
        QCOMPARE(TextLine::at(4, text).text, QString("bc"));
    }

    void codeFenceTracking()
    {
        const QString text = "a\n```\ncode\n```\nb";
        QVERIFY(!CodeFence::isCode(0, text));
        QVERIFY(CodeFence::isCode(6, text));
        QVERIFY(CodeFence::isCode(11, text)); // the closing fence line
        QVERIFY(!CodeFence::isCode(15, text));
    }

    // MARK: Return

    void returnContinuesLists()
    {
        EQ(newline("- one|"), "- one\n- |");
        EQ(newline("  * one|"), "  * one\n  * |");
        EQ(newline("- [x] done|"), "- [x] done\n- [ ] |");
        EQ(newline("1. one|"), "1. one\n2. |");
        EQ(newline("9) nine|"), "9) nine\n10) |");
    }

    void returnSplitsAnItemAtTheCaret() { EQ(newline("- one| two"), "- one\n- | two"); }

    void returnRenumbersFollowingItems()
    {
        EQ(newline("1. a|\n2. b\n    - nested\n3. c\n\nafter"), "1. a\n2. |\n3. b\n    - nested\n4. c\n\nafter");
    }

    void returnOnEmptyItemEndsTheList()
    {
        EQ(newline("- one\n- |"), "- one\n|");
        EQ(newline("- one\n- [ ] |"), "- one\n|");
        EQ(newline("1. a\n2. |"), "1. a\n|");
    }

    void returnOnEmptyNestedItemOutdents() { EQ(newline("- a\n    - |"), "- a\n- |"); }

    void returnElsewhereIsDefault()
    {
        NIL(newline("plain|"));
        NIL(newline("# Heading|"));
        NIL(newline("|- item")); // caret inside the marker
        NIL(newline("```\n- in code|\n```"));
        NIL(ME::newline("- a", TextRange(0, 2)));
    }

    void returnClosesAnOpenedFence()
    {
        EQ(newline("```swift|"), "```swift\n|\n```");
        EQ(newline("text\n~~~~|"), "text\n~~~~\n|\n~~~~");
        NIL(newline("```|\ncode\n```")); // already closed
    }

    // MARK: Tab

    void tabIndentsListItems()
    {
        EQ(indent("- a\n- b|"), "- a\n    - b|");
        EQ(indent("- a\n    - b|", true), "- a\n- b|");
        NIL(indent("plain|"));
        NIL(indent("- top|", true));
    }

    void tabRenumbersOrderedItems()
    {
        EQ(indent("1. a\n2. b|"), "1. a\n    1. b|");
        EQ(indent("1. a\n    1. x\n    2. b|", true), "1. a\n    1. x\n2. b|");
    }

    void tabIndentsEverySelectedListLine()
    {
        const QString text = "- a\n- b\nplain";
        const auto change = ME::indent(text, TextRange(0, 7), false);
        QVERIFY(change);
        QCOMPARE(change->applied(text), QString("    - a\n    - b\nplain"));
        QVERIFY(change->selection == TextRange(0, 15));
    }

    // MARK: Shortcuts and checkboxes

    void bracketShortcutMakesAChecklist()
    {
        EQ(render("[]", ME::shortcut(" ", TextRange(2, 0), "[]")), "- [ ] |");
        EQ(render("  [x]", ME::shortcut(" ", TextRange(5, 0), "  [x]")), "  - [x] |");
        NIL(ME::shortcut(" ", TextRange(3, 0), "a []"));
        NIL(ME::shortcut("x", TextRange(2, 0), "[]"));
        NIL(ME::shortcut(" ", TextRange(6, 0), "```\n[]"));
    }

    void checkboxToggle()
    {
        const QString text = "intro\n- [ ] task\n- [x] done";
        QVERIFY(ME::checkboxRange(10, text) == TextRange(8, 3));
        QVERIFY(!ME::checkboxRange(2, text));
        const TextRange keep(1, 0);
        QCOMPARE(ME::toggleCheckbox(9, text, keep)->applied(text), QString("intro\n- [x] task\n- [x] done"));
        QCOMPARE(ME::toggleCheckbox(20, text, keep)->applied(text), QString("intro\n- [ ] task\n- [ ] done"));
        QVERIFY(ME::toggleCheckbox(9, text, keep)->selection == keep);
    }

    // MARK: Slash commands

    void slashRewritesTheLinePrefix()
    {
        EQ(slashText(SlashCommand::heading1, "intro\n/h1|", "/h1"), "intro\n# |");
        EQ(slashText(SlashCommand::heading2, "Title /h|", "/h"), "## Title |");
        EQ(slashText(SlashCommand::heading3, "# Big /h3|", "/h3"), "### Big |");
        EQ(slashText(SlashCommand::bulletList, "  thing /b|", "/b"), "  - thing |");
        EQ(slashText(SlashCommand::checklist, "- item /to|", "/to"), "- [ ] item |");
        EQ(slashText(SlashCommand::quote, "## said /q|", "/q"), "> said |");
        EQ(slashText(SlashCommand::numberedList, "1. a\n/num|", "/num"), "1. a\n2. |");
    }

    void slashCodeBlockAndDivider()
    {
        EQ(slashText(SlashCommand::codeBlock, "/code|", "/code"), "```\n|\n```");
        EQ(slashText(SlashCommand::codeBlock, "let x /c|", "/c"), "```\nlet x |\n```");
        EQ(slashText(SlashCommand::divider, "para\n/div|", "/div"), "para\n\n---\n|");
        EQ(slashText(SlashCommand::divider, "/div|", "/div"), "---\n|");
        EQ(slashText(SlashCommand::divider, "text /div|\nnext", "/div"), "text \n\n---\n|\nnext");
    }

    void slashDrawingAndNewPageRemoveTheCommand()
    {
        const TextChange removal{TextRange(3, 3), QString(), TextRange(3, 0)};
        auto drawing = slash(SlashCommand::drawing, "ab /dr|", "/dr");
        QVERIFY(drawing.kind == ME::SlashEffect::Kind::insertDrawing && drawing.change == removal);
        auto page = slash(SlashCommand::newPage, "ab /ne|", "/ne");
        QVERIFY(page.kind == ME::SlashEffect::Kind::newPage && page.change == removal);
    }

    void slashOpensOnlyAtLineStartOrAfterSpace()
    {
        QVERIFY(ME::opensSlashMenu(0, ""));
        QVERIFY(ME::opensSlashMenu(3, "ab "));
        QVERIFY(ME::opensSlashMenu(3, "ab\n"));
        QVERIFY(!ME::opensSlashMenu(2, "ab")); // a/b paths
        QVERIFY(!ME::opensSlashMenu(4, "```\n"));
    }

    void slashCommandMatching()
    {
        using namespace SlashCommands;
        QVERIFY(matching("") == all());
        QVERIFY(matching("h2").first() == SlashCommand::heading2);
        QVERIFY(matching("todo") == QList<SlashCommand>{SlashCommand::checklist});
        QVERIFY(matching("num").first() == SlashCommand::numberedList);
        QVERIFY(matching("draw").first() == SlashCommand::drawing);
        QVERIFY(matching("new p") == QList<SlashCommand>{SlashCommand::newPage});
        QVERIFY(matching("code").first() == SlashCommand::codeBlock);
        QVERIFY(matching("zzz").isEmpty());
    }

    void slashMenuStateFollowsTheText()
    {
        SlashMenuState state(2);
        QVERIFY(state.update("a /", 3));
        QCOMPARE(state.items().size(), SlashCommands::all().size());
        QVERIFY(state.update("a /he", 5));
        QCOMPARE(state.query(), QString("he"));
        QVERIFY(state.selected() == SlashCommand::heading1);
        state.moveSelection(1);
        QVERIFY(state.selected() == SlashCommand::heading2);
        state.moveSelection(-2);
        QVERIFY(state.selected() == state.items().last());
        QVERIFY(state.typedRange() == TextRange(2, 3));
        QVERIFY(state.update("a /zz", 5)); // no results stays open
        QVERIFY(!state.selected());
        QVERIFY(!state.update("a /zz ", 6)); // closes on a space after no results
        QVERIFY(!state.update("a /he", 2)); // caret before the slash
        QVERIFY(!state.update("a he", 4)); // slash deleted
        QVERIFY(!state.update("a /h\nx", 6)); // newline
    }

    // MARK: Caret transform

    void caretFollowsExternalEdits()
    {
        const QString old = "hello world";
        QVERIFY(CaretTransform::transform(TextRange(8, 0), old, "say hello world") == TextRange(12, 0));
        QVERIFY(CaretTransform::transform(TextRange(2, 0), old, "hello there world") == TextRange(2, 0));
        QVERIFY(CaretTransform::transform(TextRange(11, 0), old, "hello") == TextRange(5, 0));
        QVERIFY(CaretTransform::transform(TextRange(6, 5), old, "hello big world") == TextRange(6, 9));
    }
};

QTEST_APPLESS_MAIN(MarkdownEditingTests)
#include "MarkdownEditingTests.moc"
