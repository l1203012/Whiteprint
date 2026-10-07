#include "app/ViewPreferences.h"

#include "app/AppDefaults.h"
#include "app/MacStyle.h"

namespace wp {

ViewPreferences &ViewPreferences::shared()
{
    static ViewPreferences preferences(AppDefaults::store());
    return preferences;
}

ViewPreferences::ViewPreferences(QSettings &defaults, QObject *parent) : QObject(parent), m_defaults(defaults) {}

ViewLayout ViewPreferences::layoutMode() const
{
    return AppDefaults::string(m_defaults, layoutKey).value_or(QString()) == QLatin1String("a4") ? ViewLayout::a4
                                                                                                  : ViewLayout::slides;
}

void ViewPreferences::setLayoutMode(ViewLayout mode)
{
    if (mode == layoutMode())
        return;
    m_defaults.setValue(layoutKey, mode == ViewLayout::a4 ? QStringLiteral("a4") : QStringLiteral("slides"));
    emit changed();
}

bool ViewPreferences::showsMarkdownSyntax() const
{
    return m_defaults.value(syntaxKey, false).toBool();
}

void ViewPreferences::setShowsMarkdownSyntax(bool shows)
{
    if (shows == showsMarkdownSyntax())
        return;
    m_defaults.setValue(syntaxKey, shows);
    emit changed();
}

PageTheme ViewPreferences::pageTheme() const
{
    return PageThemes::fromRawValue(AppDefaults::string(m_defaults, themeKey).value_or(QString())).value_or(PageThemes::defaultTheme);
}

void ViewPreferences::setPageTheme(PageTheme theme)
{
    if (theme == pageTheme())
        return;
    m_defaults.setValue(themeKey, PageThemes::rawValue(theme));
    emit changed();
}

BlueprintPalette ViewPreferences::pagePalette() const
{
    return PageThemes::palette(pageTheme(), mac::isDark());
}

} // namespace wp
