#pragma once

#include <QImage>
#include <QWidget>

/**
 * @brief Окно предпросмотра 1:1: один пиксель монитора на один пиксель экрана.
 */
class PreviewWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWindow(QWidget *parent = nullptr);

    void setImage(const QImage &image);

signals:
    void hidden(); ///< окно скрыто — снять галочку в тулбаре

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QImage m_image;
};
