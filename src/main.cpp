#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMutex>
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

// CACHE_TRACE_LATENCY=1: how long each key press and click takes until a
// frame showing it is on screen, printed to stderr. For measuring how
// responsive typing and editing feel.
class LatencyProbe : public QObject
{
public:
    explicit LatencyProbe(QQuickWindow *window)
        : QObject(window)
    {
        m_clock.start();
        window->installEventFilter(this);
        connect(window, &QQuickWindow::frameSwapped, this, [this] {
            const qint64 now = m_clock.nsecsElapsed();
            QList<Event> pending;
            {
                const QMutexLocker lock(&m_mutex);
                pending.swap(m_pending);
            }
            for (const Event &e : std::as_const(pending)) {
                const QByteArray ms = QByteArray::number((now - e.at) / 1e6, 'f', 1);
                fprintf(stderr, "latency %s %s ms\n", e.what, ms.constData());
            }
        }, Qt::DirectConnection);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        const char *what = nullptr;
        // Every key press is first offered to shortcuts, and one a shortcut
        // takes never arrives as a key press
        if (event->type() == QEvent::ShortcutOverride) {
            const auto *key = static_cast<QKeyEvent *>(event);
            const bool modifier = key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift
                                  || key->key() == Qt::Key_Alt || key->key() == Qt::Key_Meta;
            if (!modifier && !key->isAutoRepeat())
                what = "key";
        } else if (event->type() == QEvent::MouseButtonPress) {
            what = "click";
        }
        if (what) {
            const QMutexLocker lock(&m_mutex);
            m_pending.append({what, m_clock.nsecsElapsed()});
        }
        return false;
    }

private:
    struct Event {
        const char *what;
        qint64 at;
    };
    QElapsedTimer m_clock;
    QMutex m_mutex; // frames are swapped on the render thread
    QList<Event> m_pending;
};

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

    if (qEnvironmentVariableIsSet("CACHE_TRACE_LATENCY")) {
        const auto roots = engine.rootObjects();
        if (auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first()))
            new LatencyProbe(window);
    }

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
