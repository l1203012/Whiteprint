#pragma once
// Port of ViewPreferences.swift.
#include "render/Palette.h"

#include <QObject>
#include <QSettings>
#include <QString>

namespace wp {

/// The editor's page layout. The stored raw values are `slides` and `a4`; the UI maps this to the
/// editor module's PageLayoutMode.
enum class ViewLayout { slides, a4 };

/// App-wide editor display options from the View menu: page layout, page theme and whether Markdown
/// syntax is shown. Every open editor connects to `changed()` and applies the new values.
class ViewPreferences : public QObject {
    Q_OBJECT
public:
    static inline const QString layoutKey = QStringLiteral("PageLayout");
    static inline const QString syntaxKey = QStringLiteral("ShowMarkdownSyntax");
    static inline const QString themeKey = QStringLiteral("PageTheme");

    /// The app-wide instance on AppDefaults::store().
    static ViewPreferences &shared();

    /// `defaults` must outlive the object.
    explicit ViewPreferences(QSettings &defaults, QObject *parent = nullptr);

    ViewLayout layoutMode() const;
    /// Emits `changed()` only when the value differs.
    void setLayoutMode(ViewLayout mode);

    bool showsMarkdownSyntax() const;
    void setShowsMarkdownSyntax(bool shows);

    /// Paper unless another theme is stored; an unknown stored value falls back to Paper.
    PageTheme pageTheme() const;
    void setPageTheme(PageTheme theme);
    /// The page theme's colours for the current Windows light/dark mode.
    BlueprintPalette pagePalette() const;

signals:
    /// A view preference changed (`viewPreferencesDidChange`).
    void changed();

private:
    QSettings &m_defaults;
};

} // namespace wp
