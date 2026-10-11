#pragma once

#include <QImage>
#include <QWidget>

/**
 * @brief 1:1 preview window: one monitor pixel per screen pixel.
 */
class PreviewWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWindow(QWidget *parent = nullptr);

    void setImage(const QImage &image);

signals:
    void hidden(); ///< window hidden — untick the toolbar action

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QImage m_image;
};
