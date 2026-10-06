// Whiteprint for Windows: the entry point. Port of WhiteprintApp.swift + AppDelegate's launch path.
#include "app/AppController.h"
#include "app/MacStyle.h"
#include "app/NoteDocuments.h"
#include "app/NoteWindow.h"
#include "render/Fonts.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QStyleHints>
#include <QTimer>

#ifndef WP_VERSION
#define WP_VERSION "0.0.0"
#endif

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral("Whiteprint"));
    QApplication::setOrganizationDomain(QStringLiteral("whiteprint.app"));
    QApplication::setApplicationName(QStringLiteral("Whiteprint"));
    QApplication::setApplicationDisplayName(QStringLiteral("Whiteprint")); // appended to every window title
    QApplication::setApplicationVersion(QStringLiteral(WP_VERSION));
    QApplication app(argc, argv);

    QStringList paths = app.arguments();
    paths.removeFirst();
    paths.removeIf([](const QString &arg) { return arg.startsWith(QLatin1Char('-')); });

    auto &controller = wp::AppController::instance();
    // One instance per user: a second launch (or a double-clicked .wprint file) hands over to the first.
    if (wp::AppController::forwardToRunningInstance(paths))
        return 0;

    app.setFont(wp::fonts::system(13));
    wp::mac::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/AppIcon.ico")));
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app, &controller] {
        wp::mac::apply(app);
        for (wp::NoteWindow *w : controller.windows())
            wp::mac::styleTitleBar(w);
    });

    controller.listenForInstances();
    controller.start();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&controller] { controller.shutdown(); });

    // Files given on the command line open as notes; otherwise (or when none opened) the newest note,
    // or the Welcome note in an empty folder.
    QTimer::singleShot(0, &app, [&controller, paths] {
        controller.openPaths(paths);
        if (wp::NoteDocuments::instance().openDocuments().isEmpty())
            controller.openStartupNote();
    });
    return app.exec();
}
