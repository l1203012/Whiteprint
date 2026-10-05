/// Text served to Claude. Kept short: it costs tokens every time it's read.
public enum WhiteprintText {}

extension WhiteprintText {
    /// The drawing language reference, served as MCP resource `whiteprint://dsl`.
    /// Keep in sync with docs/DSL.md.
    public static let dslReference = """
    Whiteprint drawing language. One statement per line, # comments. Units: grid squares (1 = 10 pt), \
    origin top-left, y down. Arguments in any order.

    Shapes (no position = placed below everything else; no label = the id, except ids like a or b2; \\n in labels = new line):
    box ID [X,Y] [WxH] ["label"]     default 10x4, grows to fit label
    circle ID [X,Y] [D|WxH] ["label"]
    db ID [X,Y] [WxH] ["label"]      database cylinder
    text [ID] [X,Y] "label"

    Lines:
    arrow a>b>c ["label"]    links shapes/groups, clipped to edges. > -> end, < <- start, <> both, - none
    line a-b                 link without heads
    arrow X,Y X,Y [X,Y…]     free arrow; line X,Y X,Y … free polyline
    path X,Y X,Y … [closed]
    dim X,Y X,Y ["label"]    dimension line, label defaults to length

    Layout:
    flow a>b>c [gap]   row + arrows; undeclared ids become boxes labelled with the id (_ = space)
    row a b c [gap] | col a b c [gap]
    group ID a b ["label"]   dashed frame

    Style: dashed thick bold, closed (path).
    Example:
    flow Client>API>Postgres
    db Postgres
    text "All traffic over HTTPS"
    """
}
