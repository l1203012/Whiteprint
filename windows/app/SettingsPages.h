#pragma once
// The four panes of the settings window (ports of AISettingsViewController,
// AppearanceSettingsViewController, NotesSettingsViewController and StudySettingsViewController). Exposed so tests can drive them with their own settings objects.
#include <QWidget>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace wp {

class AISettings;
class NotesLibrary;
class ViewPreferences;
class StudySession;

/// A vertical form with headers, notes and button rows.
class SettingsPane : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPane(QWidget *parent = nullptr);

protected:
    void header(const QString &text);
    QLabel *note(const QString &text, bool secondary = true);
    QWidget *row(const QList<QWidget *> &widgets);
    /// A selectable monospaced command with a Copy button.
    QLabel *copyable(const QString &text);
    bool confirm(const QString &message, const QString &detail, const QString &button);
    void inform(const QString &message, const QString &detail, bool warning = false);

    QVBoxLayout *m_form;
};

class AISettingsPage : public SettingsPane {
    Q_OBJECT
public:
    explicit AISettingsPage(AISettings &settings, QWidget *parent = nullptr);

    QComboBox *providerPopup() const { return m_provider; }
    QLineEdit *apiKeyField() const { return m_apiKey; }
    QLineEdit *modelField() const { return m_model; }
    QLabel *keyStatus() const { return m_keyStatus; }
    QLabel *testStatus() const { return m_testStatus; }
    QPushButton *testButton() const { return m_testButton; }
    QPushButton *addButton() const { return m_addButton; }
    QLabel *codeStatus() const { return m_codeStatus; }
    QLabel *desktopStatus() const { return m_desktopStatus; }
    QLabel *commandLabel() const { return m_command; }

    /// The same actions as the buttons (the buttons confirm first, these don't).
    void providerChanged(int index);
    bool saveKey();
    void removeKey();
    void modelChanged();
    void testConnection();
    void claudeFound(const std::optional<QString> &path);

signals:
    /// Provider or key changed: the study panel should refresh (`studySessionDidChange`).
    void studyStateChanged();

private:
    void updateKeyStatus();
    void updateDesktopStatus();
    void addToClaudeCode();
    void connectDesktop();

    AISettings &m_settings;
    QComboBox *m_provider;
    QLineEdit *m_apiKey, *m_model;
    QLabel *m_keyStatus, *m_testStatus, *m_codeStatus, *m_desktopStatus, *m_command;
    QPushButton *m_testButton, *m_addButton;
    std::optional<QString> m_claude;
    QString m_helper;
};

/// The page theme. PDF export keeps its own Blueprint and Print styles.
class AppearanceSettingsPage : public SettingsPane {
    Q_OBJECT
public:
    explicit AppearanceSettingsPage(ViewPreferences &preferences, QWidget *parent = nullptr);
    QComboBox *themePopup() const { return m_theme; }

private:
    void update();
    ViewPreferences &m_preferences;
    QComboBox *m_theme;
};

class NotesSettingsPage : public SettingsPane {
    Q_OBJECT
public:
    explicit NotesSettingsPage(NotesLibrary &library, QWidget *parent = nullptr);
    QLabel *pathLabel() const { return m_path; }
    /// Switches the notes folder (what "Choose..." does after the dialog).
    void useFolder(const QString &path);

private:
    void update();
    NotesLibrary &m_library;
    QLabel *m_path;
};

class StudySettingsPage : public SettingsPane {
    Q_OBJECT
public:
    explicit StudySettingsPage(StudySession &study, QWidget *parent = nullptr);

private:
    StudySession &m_study;
};

} // namespace wp
