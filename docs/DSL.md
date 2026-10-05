# Whiteprint drawing language

Drawings live in ```` ```wp id=d1 ```` blocks inside a `.wprint` note. One statement per line;
`#` starts a comment. Units are grid squares (1 = 10 pt), origin top-left, y grows down.
Arguments after the command can come in any order.

## Shapes

```
box ID [X,Y] [WxH] ["label"] [style]     default 10x4, grows to fit the label
circle ID [X,Y] [D | WxH] ["label"]      default 4x4
db ID [X,Y] [WxH] ["label"]              database cylinder, default 8x5
text [ID] [X,Y] "label"
```

Shapes without a position or layout go in a row below everything else. Shapes without a label show
their id (`_` → space), except one-letter ids like `a` or `b2`. Use `\n` in a label for a new line.

## Lines

```
arrow a>b>c ["label"]       links shapes or groups, clipped to their edges
line a-b                    same, no arrowheads
arrow X,Y X,Y [X,Y…]        free arrow through points
line X,Y X,Y [X,Y…]         free polyline
path X,Y X,Y … [closed]     outline, optionally closed
dim X,Y X,Y ["label"]       dimension line, label defaults to the length
```

Links: `>` `->` arrow at the end, `<` `<-` at the start, `<>` `<->` both, `-` `--` none.
`arrow a b` means `arrow a>b`.

## Layout

```
flow a>b>c [gap]     row left to right, plus arrows
row a b c [gap]      row, no arrows
col a b c [gap]      column, no arrows
group ID a b ["label"]   dashed frame around shapes
```

Ids used in a layout that aren't declared become boxes labelled with the id (`_` → space).
Shapes with an explicit position keep it. Default gap is 4.

## Style

`dashed`, `thick`, `bold` (text), `closed` (path only).

## Example

```
flow Client>API>Postgres
db Postgres
text "All traffic over HTTPS"
```
