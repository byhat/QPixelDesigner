#include <QApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // The 1:1 device frames come from a hidden QQuickWidget; the OpenGL
    // backend does not render unexposed widgets, so the whole scene graph
    // runs on the software adaptation (also pixel-identical to the layout
    // renderer). An explicit user override still wins.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    // Board parity: the qpast7735 target runs the Basic Quick Controls
    // style, so previews must too (a desktop theme could pick another
    // style with different paddings and implicit sizes).
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QPixelDesigner"));
    QApplication::setOrganizationName(QStringLiteral("pixui"));

    // Board parity: the target has no platform theme, so scenes there
    // always resolve the Light color scheme; force the same here, otherwise
    // a dark desktop palette silently flips scene colors.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    MainWindow w;
    w.resize(1180, 780);
    w.show();
    return app.exec();
}
