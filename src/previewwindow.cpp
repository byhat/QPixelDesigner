#include "previewwindow.h"

#include <QHideEvent>
#include <QPainter>

PreviewWindow::PreviewWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(tr("Preview 1:1"));
}

void PreviewWindow::setImage(const QImage &image)
{
    m_image = image;
    if (!m_image.isNull())
        setFixedSize(m_image.size());
    setWindowTitle(tr("Preview 1:1 — %1x%2 px")
                       .arg(m_image.width()).arg(m_image.height()));
    update();
}

void PreviewWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_image.isNull())
        p.drawImage(0, 0, m_image);
}

void PreviewWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit hidden();
}
