#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

#include "Fonts.h"

Q_IMPORT_QML_PLUGIN(IgittStylePlugin)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("NoteCanvas"));
    QGuiApplication::setApplicationName(QStringLiteral("Note Canvas"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("notecanvas"));

    // igitt's style and themes; what it leaves out comes from Basic.
    QQuickStyle::setStyle(QStringLiteral("IgittStyle"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));
    Fonts::registerBundled();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("NoteApp", "Main");

    return app.exec();
}
