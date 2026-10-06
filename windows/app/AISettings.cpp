#include "app/AISettings.h"

#include "app/AppDefaults.h"
#include "core/TextUtil.h"

namespace wp {

QString aiProviderRawValue(AIProvider provider)
{
    return provider == AIProvider::claudeCode ? QStringLiteral("claudeCode") : QStringLiteral("grok");
}

QString aiProviderTitle(AIProvider provider)
{
    return provider == AIProvider::claudeCode ? QStringLiteral("Claude Code (your subscription)")
                                              : QStringLiteral("Grok (xAI API key)");
}

QString aiProviderName(AIProvider provider)
{
    return provider == AIProvider::claudeCode ? QStringLiteral("Claude Code") : QStringLiteral("Grok");
}

AISettings &AISettings::shared()
{
    static AISettings settings(AppDefaults::store(), QStringLiteral("io.github.l1203012.whiteprint"));
    return settings;
}

AISettings::AISettings(QSettings &defaults, const QString &keychainService)
    : apiKeyItem{keychainService, QStringLiteral("xai-api-key")}, m_defaults(defaults)
{
}

AIProvider AISettings::provider() const
{
    const auto raw = AppDefaults::string(m_defaults, providerKey);
    for (const AIProvider provider : allAIProviders)
        if (raw && *raw == aiProviderRawValue(provider))
            return provider;
    return AIProvider::claudeCode;
}

void AISettings::setProvider(AIProvider provider)
{
    m_defaults.setValue(providerKey, aiProviderRawValue(provider));
}

QString AISettings::grokModel() const
{
    const QString model = trimmedWhitespace(AppDefaults::string(m_defaults, grokModelKey).value_or(QString()));
    return model.isEmpty() ? GrokRunner::Configuration::defaultModel() : model;
}

void AISettings::setGrokModel(const QString &value)
{
    const QString model = trimmedWhitespace(value);
    if (model.isEmpty() || model == GrokRunner::Configuration::defaultModel())
        m_defaults.remove(grokModelKey);
    else
        m_defaults.setValue(grokModelKey, model);
}

std::optional<GrokRunner::Configuration> AISettings::grokConfiguration() const
{
    const auto key = apiKeyItem.read();
    if (!key)
        return std::nullopt;
    const QString trimmed = key->trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    GrokRunner::Configuration configuration;
    configuration.apiKey = trimmed;
    configuration.model = grokModel();
    return configuration;
}

} // namespace wp
