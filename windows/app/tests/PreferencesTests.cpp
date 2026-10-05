// Port of PreferencesTests.swift (the Touch Bar test has no Windows counterpart).
#include "app/AISettings.h"
#include "app/ViewPreferences.h"

#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace wp;

class PreferencesTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<QSettings> m_defaults;
    QString m_service = "io.github.l1203012.whiteprint.tests." + QUuid::createUuid().toString(QUuid::WithoutBraces);

private slots:
    void init()
    {
        m_defaults = std::make_unique<QSettings>(m_dir.filePath(QUuid::createUuid().toString(QUuid::Id128) + ".ini"), QSettings::IniFormat);
    }

    void cleanup() { KeychainItem{m_service, "xai-api-key"}.remove(); }

    void viewDefaultsAndChanges()
    {
        ViewPreferences preferences(*m_defaults);
        QCOMPARE(preferences.layoutMode(), ViewLayout::slides);
        QVERIFY(!preferences.showsMarkdownSyntax());
        QSignalSpy spy(&preferences, &ViewPreferences::changed);
        preferences.setLayoutMode(ViewLayout::a4);
        preferences.setLayoutMode(ViewLayout::a4);
        preferences.setShowsMarkdownSyntax(true);
        QCOMPARE(spy.count(), 2);
        m_defaults->sync();
        QCOMPARE(ViewPreferences(*m_defaults).layoutMode(), ViewLayout::a4);
        QVERIFY(ViewPreferences(*m_defaults).showsMarkdownSyntax());
    }

    void providerAndModel()
    {
        AISettings settings(*m_defaults, m_service);
        QVERIFY(settings.provider() == AIProvider::claudeCode);
        QCOMPARE(settings.grokModel(), GrokRunner::Configuration::defaultModel());
        settings.setProvider(AIProvider::grok);
        settings.setGrokModel("  grok-4-fast ");
        AISettings reloaded(*m_defaults, m_service);
        QVERIFY(reloaded.provider() == AIProvider::grok);
        QCOMPARE(reloaded.grokModel(), QStringLiteral("grok-4-fast"));
        reloaded.setGrokModel("");
        QCOMPARE(reloaded.grokModel(), GrokRunner::Configuration::defaultModel());
    }

    void keyLivesInTheCredentialManagerOnly()
    {
        AISettings settings(*m_defaults, m_service);
        QVERIFY(!settings.grokConfiguration());
        settings.apiKeyItem.save("xai-first");
        settings.apiKeyItem.save(" xai-second\n");
        QCOMPARE(*settings.apiKeyItem.read(), QStringLiteral(" xai-second\n"));
        const auto configuration = settings.grokConfiguration();
        QVERIFY(configuration);
        QCOMPARE(configuration->apiKey, QStringLiteral("xai-second"));
        QCOMPARE(configuration->model, GrokRunner::Configuration::defaultModel());
        for (const QString &k : m_defaults->allKeys())
            QVERIFY(!m_defaults->value(k).toString().contains("xai-second"));
        settings.apiKeyItem.remove();
        QVERIFY(!settings.apiKeyItem.read());
        QVERIFY(!settings.grokConfiguration());
        settings.apiKeyItem.remove();
    }
};

QTEST_GUILESS_MAIN(PreferencesTests)
#include "PreferencesTests.moc"
