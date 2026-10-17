#include <QApplication>
#include <QQuickWindow>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // The 1:1 device frames come from a hidden QQuickWidget; the OpenGL
    // backend does not render unexposed widgets, so the whole scene graph
    // runs on the software adaptation (also pixel-identical to the layout
    // renderer). An explicit user override still wins.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QPixelDesigner"));
    QApplication::setOrganizationName(QStringLiteral("pixui"));

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    MainWindow w;
    w.resize(1180, 780);
    w.show();
    return app.exec();
}
