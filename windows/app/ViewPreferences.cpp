#include "app/ViewPreferences.h"

#include "app/AppDefaults.h"

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

} // namespace wp
