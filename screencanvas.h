#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QWidget>

#include "screenelement.h"

/**
 * @brief Холст: экран, увеличенный в 1..24 раза, с видимой пиксельной сеткой.
 *
 * Модель элементов принадлежит MainWindow и передаётся указателем. Поддерживает
 * множественное выделение (Shift+клик, рамка), перетаскивание, растяжение за
 * углы; элементы одной группы перемещаются вместе.
 */
class ScreenCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit ScreenCanvas(QWidget *parent = nullptr);

    void setModel(std::vector<Element> *elements);
    void setScreenSize(int width, int height);
    void setBackgroundColor(const QColor &color);
    void setZoom(int zoom); ///< виджет-пикселей на один пиксель экрана
    int zoom() const { return m_zoom; }
    void setShowGrid(bool on);

    void setSelection(const QSet<int> &sel); ///< заменить выделение на набор индексов
    void clearSelection();
    const QSet<int> &selection() const { return m_sel; }
    int selectionCount() const { return m_sel.size(); }
    int primaryIndex() const; ///< единственный выделенный элемент или -1

    void setExternalImage(const QImage &img); ///< рисовать кадр QML вместо модели
    QImage renderScreenImage() const;         ///< рендер модели 1:1

    QSize sizeHint() const override;

signals:
    void selectionChanged();
    void modelChanged(); ///< модель изменена мышью
    void hoverChanged(const QPoint &screenPos);
    void zoomChanged(int zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    /** @brief Активное действие мыши. */
    enum class Drag { None, Move, Resize, Rubber };

    QPointF toScreen(const QPoint &widgetPos) const; ///< координаты виджета -> экрана
    QRect widgetRect(const QRect &screenRect) const; ///< экрана -> виджета
    int cornerAt(const QPoint &widgetPos) const;     ///< маска угла: 1=лево 2=верх 4=право 8=низ
    std::vector<int> expandWithGroups(const QSet<int> &sel) const; ///< сел + члены их групп

    std::vector<Element> *m_elements = nullptr;
    int m_screenW = 128;
    int m_screenH = 160;
    QColor m_bg = Qt::white;
    int m_zoom = 4;
    bool m_showGrid = true;

    QSet<int> m_sel;        ///< выделенные индексы модели
    int m_primary = -1;     ///< индекс для панели свойств
    QImage m_external;      ///< кадр QML-режима; null — рисовать модель

    Drag m_drag = Drag::None;
    int m_corner = 0;
    QRect m_resizeStartRect;
    std::vector<int> m_moveSet;
    std::vector<QRect> m_moveStartRects;
    QRectF m_rubber;
    QPointF m_pressScreen;
    bool m_dragMoved = false;

    static constexpr int Margin = 24; ///< отступ экрана от краёв виджета, px
};
