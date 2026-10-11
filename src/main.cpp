#include <QApplication>
#include <QQuickWindow>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QPixelDesigner"));
    QApplication::setOrganizationName(QStringLiteral("pixui"));

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    MainWindow w;
    w.resize(1180, 780);
    w.show();
    return app.exec();
}
