#pragma once
// Port of AISettings.swift.
#include "app/KeychainItem.h"
#include "study/GrokRunner.h"

#include <QSettings>
#include <QString>
#include <optional>

namespace wp {

/// Which AI builds study plans.
enum class AIProvider { claudeCode, grok };

/// All providers, in menu order.
inline constexpr AIProvider allAIProviders[] = {AIProvider::claudeCode, AIProvider::grok};

/// The value stored in settings: `claudeCode`, `grok`.
QString aiProviderRawValue(AIProvider provider);
/// `Claude Code (your subscription)` / `Grok (xAI API key)`.
QString aiProviderTitle(AIProvider provider);
/// `Claude Code` / `Grok`.
QString aiProviderName(AIProvider provider);

/// The AI provider choice and the Grok model live in the settings store; the xAI API key only ever
/// lives in Windows Credential Manager.
class AISettings {
public:
    static inline const QString providerKey = QStringLiteral("AIProvider");
    static inline const QString grokModelKey = QStringLiteral("GrokModel");

    /// The app-wide instance: AppDefaults::store() and the service `io.github.l1203012.whiteprint`.
    static AISettings &shared();

    /// `defaults` must outlive the object.
    AISettings(QSettings &defaults, const QString &keychainService);

    KeychainItem apiKeyItem;

    AIProvider provider() const;
    void setProvider(AIProvider provider);

    /// The model to use; the default when none (or only whitespace) was set.
    QString grokModel() const;
    /// Whitespace is trimmed; an empty or default model clears the stored value.
    void setGrokModel(const QString &model);

    /// The configuration for a Grok run, or nullopt without an API key.
    std::optional<GrokRunner::Configuration> grokConfiguration() const;

private:
    QSettings &m_defaults;
};

} // namespace wp
