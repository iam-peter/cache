#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "Fonts.h"
#include "workspace.h"

Q_IMPORT_QML_PLUGIN(IgittStylePlugin)

namespace {

// Cache used to be called Note Canvas. On the first start its settings
// carry over, and its workspace stays where it is when there is no
// ~/Documents/Cache yet: nothing is moved or deleted.
void carryOverNoteCanvas()
{
    QSettings settings;
    if (!settings.allKeys().isEmpty())
        return;
    QSettings old(QStringLiteral("NoteCanvas"), QStringLiteral("Note Canvas"));
    const QStringList keys = old.allKeys();
    for (const QString &key : keys)
        settings.setValue(key, old.value(key));
    if (!settings.contains(QStringLiteral("workspace/root"))) {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString oldRoot = documents + QStringLiteral("/Note Canvas");
        if (QFileInfo(oldRoot).isDir() && !QFileInfo::exists(documents + QStringLiteral("/Cache")))
            settings.setValue(QStringLiteral("workspace/root"), oldRoot);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Cache"));
    QGuiApplication::setApplicationName(QStringLiteral("Cache"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    QGuiApplication::setDesktopFileName(QStringLiteral("cache"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Cache/logo/cache.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Notes on an infinite canvas"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption workspaceOption(QStringLiteral("workspace"),
                                             QStringLiteral("Workspace folder for this start (not saved)."),
                                             QStringLiteral("folder"));
    // For screenshots and the smoke tests of the packages
    const QCommandLineOption grabOption(QStringLiteral("grab"),
                                        QStringLiteral("Save a screenshot of the window and quit."),
                                        QStringLiteral("png"));
    parser.addOptions({workspaceOption, grabOption});
    parser.process(app);

    carryOverNoteCanvas();
    if (parser.isSet(workspaceOption))
        Workspace::setStartRoot(QDir(parser.value(workspaceOption)).absolutePath());

    // igitt's style and themes; what it leaves out comes from Basic.
    // Smooth connectors, shapes and pen lines
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QQuickStyle::setStyle(QStringLiteral("IgittStyle"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));
    Fonts::registerBundled();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Cache", "Main");

    if (parser.isSet(grabOption)) {
        const QString out = parser.value(grabOption);
        QTimer::singleShot(1500, &app, [&engine, out] {
            const auto roots = engine.rootObjects();
            auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
            const bool saved = window && window->grabWindow().save(out);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
