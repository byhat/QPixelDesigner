#include "screencanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <set>

ScreenCanvas::ScreenCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ScreenCanvas::setModel(std::vector<Element> *elements)
{
    m_elements = elements;
    clearSelection();
}

void ScreenCanvas::setScreenSize(int width, int height)
{
    m_screenW = width;
    m_screenH = height;
    resize(sizeHint());
    update();
}

void ScreenCanvas::setBackgroundColor(const QColor &color)
{
    m_bg = color;
    update();
}

void ScreenCanvas::setZoom(int zoom)
{
    m_zoom = qBound(1, zoom, 24);
    resize(sizeHint());
    update();
}

void ScreenCanvas::setShowGrid(bool on)
{
    m_showGrid = on;
    update();
}

void ScreenCanvas::setSelection(const QSet<int> &sel)
{
    QSet<int> s;
    if (m_elements) {
        for (int i : sel)
            if (i >= 0 && i < int(m_elements->size()))
                s.insert(i);
    }
    m_sel = s;
    m_primary = m_sel.isEmpty() ? -1 : *std::max_element(m_sel.cbegin(), m_sel.cend());
    update();
    emit selectionChanged();
}

void ScreenCanvas::clearSelection()
{
    m_sel.clear();
    m_primary = -1;
    update();
    emit selectionChanged();
}

int ScreenCanvas::primaryIndex() const
{
    if (m_elements && m_primary >= 0 && m_primary < int(m_elements->size()))
        return m_primary;
    return -1;
}

void ScreenCanvas::setExternalImage(const QImage &img)
{
    m_external = img;
    update();
}

QImage ScreenCanvas::renderScreenImage() const
{
    if (!m_elements)
        return QImage();
    return renderScreen(*m_elements, m_screenW, m_screenH, m_bg);
}

QSize ScreenCanvas::sizeHint() const
{
    return QSize(m_screenW * m_zoom + 2 * Margin, m_screenH * m_zoom + 2 * Margin);
}

QPointF ScreenCanvas::toScreen(const QPoint &p) const
{
    return QPointF(double(p.x() - Margin) / m_zoom, double(p.y() - Margin) / m_zoom);
}

QRect ScreenCanvas::widgetRect(const QRect &r) const
{
    return QRect(Margin + r.x() * m_zoom, Margin + r.y() * m_zoom,
                 qMax(1, r.width()) * m_zoom, qMax(1, r.height()) * m_zoom);
}

std::vector<int> ScreenCanvas::expandWithGroups(const QSet<int> &sel) const
{
    std::set<int> out(sel.cbegin(), sel.cend());
    if (m_elements) {
        for (int i : sel) {
            const int g = (*m_elements)[i].group;
            if (g > 0) {
                for (int j = 0; j < int(m_elements->size()); ++j)
                    if ((*m_elements)[j].group == g)
                        out.insert(j);
            }
        }
    }
    return std::vector<int>(out.begin(), out.end());
}

int ScreenCanvas::cornerAt(const QPoint &wp) const
{
    if (!m_elements || m_sel.size() != 1)
        return 0;
    const Element &e = (*m_elements)[m_primary];
    if (e.kind == ElementKind::Text)
        return 0;

    const QRect wr = widgetRect(e.visualRect());
    const int r = 7;
    struct Corner { int x, y, bits; };
    const Corner cs[4] = {
        {wr.left(),  wr.top(),    1 | 2},
        {wr.right(), wr.top(),    4 | 2},
        {wr.left(),  wr.bottom(), 1 | 8},
        {wr.right(), wr.bottom(), 4 | 8},
    };
    int best = 0;
    int bestDist = r * r;
    for (const Corner &c : cs) {
        const int dx = wp.x() - c.x;
        const int dy = wp.y() - c.y;
        const int d = dx * dx + dy * dy;
        if (d <= bestDist) {
            bestDist = d;
            best = c.bits;
        }
    }
    return best;
}

void ScreenCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(43, 48, 58));

    const QRect sr(Margin, Margin, m_screenW * m_zoom, m_screenH * m_zoom);
    QImage frame = m_external;
    if (frame.isNull())
        frame = renderScreenImage();
    if (frame.size() != QSize(m_screenW, m_screenH))
        frame = frame.scaled(m_screenW, m_screenH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    p.drawImage(sr, frame);
    p.setPen(QPen(QColor(130, 140, 155), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(sr.adjusted(-1, -1, 1, 1));

    if (m_showGrid && m_zoom >= 4) {
        for (int x = 1; x < m_screenW; ++x) {
            const bool major = (x % 8 == 0);
            p.setPen(major ? QColor(255, 255, 255, 55) : QColor(0, 0, 0, 45));
            const int px = sr.left() + x * m_zoom;
            p.drawLine(px, sr.top(), px, sr.bottom() - 1);
        }
        for (int y = 1; y < m_screenH; ++y) {
            const bool major = (y % 8 == 0);
            p.setPen(major ? QColor(255, 255, 255, 55) : QColor(0, 0, 0, 45));
            const int py = sr.top() + y * m_zoom;
            p.drawLine(sr.left(), py, sr.right() - 1, py);
        }
    }

    if (!m_sel.isEmpty() && m_elements) {
        p.setBrush(Qt::NoBrush);
        for (int i : m_sel) {
            const QRect wr = widgetRect((*m_elements)[i].visualRect());
            p.setPen(QPen(i == m_primary ? QColor(255, 170, 0) : QColor(255, 215, 110),
                          1, Qt::DashLine));
            p.drawRect(wr.adjusted(-2, -2, 2, 2));
        }
        if (m_sel.size() == 1 && (*m_elements)[m_primary].kind != ElementKind::Text) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 170, 0));
            const QRect wr = widgetRect((*m_elements)[m_primary].visualRect());
            const QPoint pts[4] = {wr.topLeft(), wr.topRight(),
                                   wr.bottomLeft(), wr.bottomRight()};
            for (const QPoint &c : pts)
                p.drawRect(QRect(c - QPoint(3, 3), QSize(7, 7)));
        }
    }

    if (m_drag == Drag::Rubber && m_dragMoved) {
        const QRect rw = widgetRect(m_rubber.toAlignedRect());
        p.setPen(QPen(QColor(120, 170, 255), 1, Qt::DashLine));
        p.setBrush(QColor(120, 170, 255, 30));
        p.drawRect(rw);
    }
}

void ScreenCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_elements) {
        QWidget::mousePressEvent(e);
        return;
    }

    const QPoint wp = e->position().toPoint();
    const QPointF sp = toScreen(wp);
    const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;

    const int corner = cornerAt(wp);
    if (corner != 0 && !shift) {
        m_drag = Drag::Resize;
        m_corner = corner;
        m_resizeStartRect = (*m_elements)[m_primary].rect;
        m_pressScreen = sp;
        m_dragMoved = false;
        return;
    }

    int hit = -1;
    for (int i = int(m_elements->size()) - 1; i >= 0; --i) {
        if ((*m_elements)[i].hitTest(sp)) {
            hit = i;
            break;
        }
    }

    if (shift && hit >= 0) {
        if (m_sel.contains(hit))
            m_sel.remove(hit);
        else
            m_sel.insert(hit);
        m_primary = m_sel.contains(hit)
                        ? hit
                        : (m_sel.isEmpty() ? -1 : *std::max_element(m_sel.cbegin(), m_sel.cend()));
        update();
        emit selectionChanged();
        return;
    }

    if (hit >= 0) {
        if (!m_sel.contains(hit)) {
            m_sel = QSet<int>{hit};
            update();
            emit selectionChanged();
        }
        m_primary = hit;
        m_moveSet = expandWithGroups(m_sel);
        m_moveStartRects.clear();
        for (int i : m_moveSet)
            m_moveStartRects.push_back((*m_elements)[i].rect);
        m_drag = Drag::Move;
        m_pressScreen = sp;
        m_dragMoved = false;
    } else {
        if (!shift) {
            m_sel.clear();
            m_primary = -1;
            update();
            emit selectionChanged();
        }
        m_drag = Drag::Rubber;
        m_rubber = QRectF(sp, sp);
        m_pressScreen = sp;
        m_dragMoved = false;
    }
}

void ScreenCanvas::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint wp = e->position().toPoint();
    const QPointF sp = toScreen(wp);
    emit hoverChanged(QPoint(int(sp.x()), int(sp.y())));

    if (m_drag == Drag::None) {
        const int c = cornerAt(wp);
        if (c != 0) {
            setCursor(c == 3 || c == 12 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        } else {
            bool over = false;
            if (m_elements) {
                for (int i = int(m_elements->size()) - 1; i >= 0; --i) {
                    if ((*m_elements)[i].hitTest(sp)) {
                        over = true;
                        break;
                    }
                }
            }
            setCursor(over ? Qt::SizeAllCursor : Qt::ArrowCursor);
        }
        return;
    }

    if (m_drag == Drag::Rubber) {
        m_rubber = QRectF(m_pressScreen, sp).normalized();
        m_dragMoved = true;
        update();
        return;
    }

    if (!m_elements || (m_drag == Drag::Move && m_moveSet.empty()))
        return;
    if (m_drag == Drag::Resize && primaryIndex() < 0)
        return;

    const QPointF d = sp - m_pressScreen;
    const int dx = int(std::round(d.x()));
    const int dy = int(std::round(d.y()));

    if (m_drag == Drag::Move) {
        for (size_t k = 0; k < m_moveSet.size(); ++k)
            (*m_elements)[m_moveSet[k]].rect = m_moveStartRects[k].translated(dx, dy);
        m_dragMoved = true;
        update();
        emit modelChanged();
        return;
    }

    Element &el = (*m_elements)[m_primary];
    QPoint orig((m_corner & 4) ? m_resizeStartRect.right() : m_resizeStartRect.left(),
                (m_corner & 8) ? m_resizeStartRect.bottom() : m_resizeStartRect.top());
    const QPoint np(orig.x() + dx, orig.y() + dy);
    QRect r = m_resizeStartRect;
    if (m_corner & 1) r.setLeft(np.x());
    if (m_corner & 2) r.setTop(np.y());
    if (m_corner & 4) r.setRight(np.x());
    if (m_corner & 8) r.setBottom(np.y());
    r = r.normalized();
    if (el.kind != ElementKind::Line) {
        if (r.width() < 1)  r.setWidth(1);
        if (r.height() < 1) r.setHeight(1);
    }
    el.rect = r;
    m_dragMoved = true;
    update();
    emit modelChanged();
}

void ScreenCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag == Drag::Rubber) {
        if (m_dragMoved && m_elements) {
            std::set<int> found;
            for (int i = 0; i < int(m_elements->size()); ++i) {
                if (QRectF((*m_elements)[i].visualRect()).intersects(m_rubber))
                    found.insert(i);
            }
            const std::vector<int> expanded =
                expandWithGroups(QSet<int>(found.begin(), found.end()));
            QSet<int> sel(expanded.begin(), expanded.end());
            if (e->modifiers() & Qt::ShiftModifier)
                sel.unite(m_sel);
            m_sel = sel;
            m_primary = m_sel.isEmpty() ? -1
                                        : *std::max_element(m_sel.cbegin(), m_sel.cend());
            emit selectionChanged();
        }
        m_drag = Drag::None;
        update();
        return;
    }

    if (m_drag != Drag::None) {
        m_drag = Drag::None;
        m_corner = 0;
    }
}

void ScreenCanvas::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        emit zoomChanged(m_zoom + (e->angleDelta().y() > 0 ? 1 : -1));
        e->accept();
    } else {
        QWidget::wheelEvent(e);
    }
}
