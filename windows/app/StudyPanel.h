#pragma once
// Port of StudyPanelController.swift: drop course material in, watch extraction, and start the chosen
// AI provider to turn it into a study plan. Driven by AppServices::shared().study().
#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace wp {

class StudySession;
class DropZone;

class StudyPanel : public QWidget {
    Q_OBJECT
public:
    /// Shows the panel in its own window (a child window of `over` when given) or raises the open one.
    /// With `generating`, starts a run as soon as there is material and the provider is ready.
    static void present(QWidget *over, bool generating = false);

    /// `session` defaults to AppServices::shared().study().
    explicit StudyPanel(QWidget *parent = nullptr, StudySession *session = nullptr);

    /// Starts a run when the provider is ready, there are imports and nothing is extracting.
    void generate();
    /// Like `generate`, but waits for the Claude lookup if needed (the `generating` flag of Swift).
    void generateWhenReady();

    /// What's missing before the chosen provider can run; empty when ready.
    QString providerHelp() const;

    QPushButton *generateButton() const { return m_generate; }
    QPushButton *cancelButton() const { return m_cancel; }
    QListWidget *importList() const { return m_list; }

signals:
    /// The Close button was pressed; the host (dialog, sheet or side panel) hides the panel.
    void closeRequested();

private:
    void update();
    void chooseFiles();

    StudySession *m_session;
    DropZone *m_drop;
    QListWidget *m_list;
    QLabel *m_errors, *m_privacy, *m_help;
    QPlainTextEdit *m_log;
    QPushButton *m_generate, *m_cancel;
    bool m_startWhenReady = false;
};

} // namespace wp
