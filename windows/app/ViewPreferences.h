#pragma once
// Port of ViewPreferences.swift.
#include <QObject>
#include <QSettings>
#include <QString>

namespace wp {

/// The editor's page layout. The stored raw values are `slides` and `a4`; the UI maps this to the
/// editor module's PageLayoutMode.
enum class ViewLayout { slides, a4 };

/// App-wide editor display options from the View menu: page layout and whether Markdown syntax is
/// shown. Every open editor connects to `changed()` and applies the new values.
class ViewPreferences : public QObject {
    Q_OBJECT
public:
    static inline const QString layoutKey = QStringLiteral("PageLayout");
    static inline const QString syntaxKey = QStringLiteral("ShowMarkdownSyntax");

    /// The app-wide instance on AppDefaults::store().
    static ViewPreferences &shared();

    /// `defaults` must outlive the object.
    explicit ViewPreferences(QSettings &defaults, QObject *parent = nullptr);

    ViewLayout layoutMode() const;
    /// Emits `changed()` only when the value differs.
    void setLayoutMode(ViewLayout mode);

    bool showsMarkdownSyntax() const;
    void setShowsMarkdownSyntax(bool shows);

signals:
    /// A view preference changed (`viewPreferencesDidChange`).
    void changed();

private:
    QSettings &m_defaults;
};

} // namespace wp
