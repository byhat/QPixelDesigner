#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <vector>

/** @brief Layout element type. */
enum class ElementKind {
    Rect,    ///< rectangle with optional corner rounding
    Ellipse, ///< ellipse
    Line,    ///< line segment
    Text     ///< text
};

QString kindToString(ElementKind kind);       ///< type identifier used in JSON
QString kindToLabel(ElementKind kind);        ///< human-readable type name
ElementKind kindFromString(const QString &s); ///< inverse of kindToString()

/**
 * @brief A screen interface element.
 *
 * All geometry is expressed in pixels of the target display, not in widget
 * coordinates.
 */
class Element
{
public:
    Element() = default;
    Element(ElementKind kind, const QRect &rect, const QString &name);

    ElementKind kind = ElementKind::Rect; ///< element type
    QString name;                         ///< name shown in the layer list
    QRect rect;      ///< geometry; for Line the endpoints are topLeft and bottomRight
    QColor fill = Qt::black;   ///< fill color; text color for Text
    QColor stroke = Qt::black; ///< outline or line color
    bool filled = true;        ///< fill (true) or 1 px outline only (false)
    QString text;              ///< contents of a Text element
    int fontSize = 10;         ///< font size in screen pixels
    int radius = 0;            ///< rectangle corner radius in screen pixels
    int group = 0;             ///< group id; 0 means no group

    QRect visualRect() const;             ///< occupied area, text-aware
    bool hitTest(const QPointF &p) const; ///< whether the point hits the element

    QJsonObject toJson() const;                     ///< serialize to JSON
    static Element fromJson(const QJsonObject &obj); ///< deserialize

private:
    QRect textRect() const; ///< text bounds according to font metrics
};

/** @brief Render the layout into a width x height image. */
QImage renderScreen(const std::vector<Element> &elements, int width, int height,
                    const QColor &background);

/**
 * @brief Image correction, an equivalent of the ffmpeg eq filter.
 * @param contrast   contrast multiplier
 * @param saturation saturation multiplier
 * @param brightness brightness shift in 0..255 levels
 */
QImage applyEqFilter(const QImage &src, double contrast, double saturation, int brightness);

/**
 * @brief Pack pixels into RGB565, two bytes per pixel.
 * @param bigEndian true for rgb565be, false for rgb565le
 */
QByteArray toRgb565(const QImage &img, bool bigEndian);

/** @brief C header with a byte array in the xxd -i output format. */
QString toXxdHeader(const QByteArray &bytes, const QString &arrayName,
                    int width, int height, bool bigEndian);

/**
 * @brief Normalize a file name on save.
 *
 * Spaces in the file name become '_', a missing suffix is appended
 * ("my ui" -> "my_ui.pixui.json"). The directory part is left unchanged.
 */
QString withForcedSuffix(QString path, const QString &suffix);
