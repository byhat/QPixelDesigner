#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QWidget>

#include "screenelement.h"

/**
 * @brief Canvas: the screen scaled 1..24x with a visible pixel grid.
 *
 * The element model is owned by MainWindow and passed as a pointer.
 * Supports multiple selection (Shift+click, rubber band), dragging and
 * resizing by corner handles; elements of the same group move together.
 */
class ScreenCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit ScreenCanvas(QWidget *parent = nullptr);

    void setModel(std::vector<Element> *elements);
    void setScreenSize(int width, int height);
    void setBackgroundColor(const QColor &color);
    void setZoom(int zoom); ///< widget pixels per screen pixel
    int zoom() const { return m_zoom; }
    void setShowGrid(bool on);

    void setSelection(const QSet<int> &sel); ///< replace selection with model indices
    void clearSelection();
    const QSet<int> &selection() const { return m_sel; }
    int selectionCount() const { return m_sel.size(); }
    int primaryIndex() const; ///< the only selected element, or -1

    void setExternalImage(const QImage &img); ///< draw a QML frame instead of the model
    QImage renderScreenImage() const;         ///< 1:1 model render

    QSize sizeHint() const override;

signals:
    void selectionChanged();
    void modelChanged(); ///< model modified with the mouse
    void hoverChanged(const QPoint &screenPos);
    void zoomChanged(int zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    /** @brief Active mouse action. */
    enum class Drag { None, Move, Resize, Rubber };

    QPointF toScreen(const QPoint &widgetPos) const;  ///< widget -> screen coordinates
    QRect widgetRect(const QRect &screenRect) const;  ///< screen -> widget rectangle
    int cornerAt(const QPoint &widgetPos) const;      ///< corner mask: 1=left 2=top 4=right 8=bottom
    std::vector<int> expandWithGroups(const QSet<int> &sel) const; ///< selection plus group members

    std::vector<Element> *m_elements = nullptr;
    int m_screenW = 128;
    int m_screenH = 160;
    QColor m_bg = Qt::white;
    int m_zoom = 4;
    bool m_showGrid = true;

    QSet<int> m_sel;    ///< selected model indices
    int m_primary = -1; ///< index used by the properties panel
    QImage m_external;  ///< QML-mode frame; null renders the model

    Drag m_drag = Drag::None;
    int m_corner = 0;
    QRect m_resizeStartRect;
    std::vector<int> m_moveSet;
    std::vector<QRect> m_moveStartRects;
    QRectF m_rubber;
    QPointF m_pressScreen;
    bool m_dragMoved = false;

    static constexpr int Margin = 24; ///< screen offset from widget edges, px
};
