#include "screenelement.h"

#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QPainter>

#include <cmath>

QString kindToString(ElementKind kind)
{
    switch (kind) {
    case ElementKind::Rect:    return QStringLiteral("rect");
    case ElementKind::Ellipse: return QStringLiteral("ellipse");
    case ElementKind::Line:    return QStringLiteral("line");
    case ElementKind::Text:    return QStringLiteral("text");
    }
    return QStringLiteral("rect");
}

QString kindToLabel(ElementKind kind)
{
    switch (kind) {
    case ElementKind::Rect:    return QStringLiteral("прямоугольник");
    case ElementKind::Ellipse: return QStringLiteral("эллипс");
    case ElementKind::Line:    return QStringLiteral("линия");
    case ElementKind::Text:    return QStringLiteral("текст");
    }
    return QString();
}

ElementKind kindFromString(const QString &s)
{
    if (s == QLatin1String("ellipse")) return ElementKind::Ellipse;
    if (s == QLatin1String("line"))    return ElementKind::Line;
    if (s == QLatin1String("text"))    return ElementKind::Text;
    return ElementKind::Rect;
}

Element::Element(ElementKind k, const QRect &r, const QString &n)
    : kind(k), name(n), rect(r)
{
}

QRect Element::textRect() const
{
    QFont f;
    f.setPixelSize(fontSize);
    const QFontMetrics fm(f);
    return QRect(rect.x(), rect.y(),
                 qMax(1, fm.horizontalAdvance(text)), qMax(1, fm.height()));
}

QRect Element::visualRect() const
{
    if (kind == ElementKind::Text)
        return textRect();
    return rect.normalized();
}

bool Element::hitTest(const QPointF &p) const
{
    if (kind == ElementKind::Line)
        return rect.normalized().adjusted(-2, -2, 3, 3).contains(p.toPoint());
    return visualRect().contains(p.toPoint());
}

QJsonObject Element::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("kind")] = kindToString(kind);
    obj[QStringLiteral("name")] = name;
    const QJsonArray r = {rect.x(), rect.y(), rect.width(), rect.height()};
    obj[QStringLiteral("rect")] = r;
    obj[QStringLiteral("fill")] = fill.name(QColor::HexArgb);
    obj[QStringLiteral("stroke")] = stroke.name(QColor::HexArgb);
    obj[QStringLiteral("filled")] = filled;
    obj[QStringLiteral("text")] = text;
    obj[QStringLiteral("fontSize")] = fontSize;
    obj[QStringLiteral("radius")] = radius;
    obj[QStringLiteral("group")] = group;
    return obj;
}

Element Element::fromJson(const QJsonObject &obj)
{
    Element e;
    e.kind = kindFromString(obj[QStringLiteral("kind")].toString());
    e.name = obj[QStringLiteral("name")].toString();
    const QJsonArray r = obj[QStringLiteral("rect")].toArray();
    if (r.size() == 4)
        e.rect = QRect(r.at(0).toInt(), r.at(1).toInt(), r.at(2).toInt(), r.at(3).toInt());
    e.fill = QColor(obj[QStringLiteral("fill")].toString());
    if (!e.fill.isValid()) e.fill = Qt::black;
    e.stroke = QColor(obj[QStringLiteral("stroke")].toString());
    if (!e.stroke.isValid()) e.stroke = Qt::black;
    e.filled = obj[QStringLiteral("filled")].toBool(true);
    e.text = obj[QStringLiteral("text")].toString();
    e.fontSize = obj[QStringLiteral("fontSize")].toInt(10);
    e.radius = qMax(0, obj[QStringLiteral("radius")].toInt(0));
    e.group = obj[QStringLiteral("group")].toInt(0);
    return e;
}

QImage renderScreen(const std::vector<Element> &elements, int width, int height,
                    const QColor &background)
{
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(background);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, false);

    for (const Element &e : elements) {
        switch (e.kind) {
        case ElementKind::Rect:
            if (e.filled) {
                p.setPen(Qt::NoPen);
                p.setBrush(e.fill);
            } else {
                p.setPen(QPen(e.stroke, 1));
                p.setBrush(Qt::NoBrush);
            }
            if (e.radius > 0)
                p.drawRoundedRect(e.rect, e.radius, e.radius);
            else
                p.drawRect(e.rect);
            break;
        case ElementKind::Ellipse:
            if (e.filled) {
                p.setPen(Qt::NoPen);
                p.setBrush(e.fill);
            } else {
                p.setPen(QPen(e.stroke, 1));
                p.setBrush(Qt::NoBrush);
            }
            p.drawEllipse(e.rect);
            break;
        case ElementKind::Line:
            p.setPen(QPen(e.stroke, 1));
            p.drawLine(e.rect.topLeft(), e.rect.bottomRight());
            break;
        case ElementKind::Text: {
            QFont f = p.font();
            f.setPixelSize(e.fontSize);
            p.setFont(f);
            p.setPen(e.fill);
            const QFontMetrics fm(f);
            p.drawText(e.rect.x(), e.rect.y() + fm.ascent(), e.text);
            break;
        }
        }
    }
    return img;
}

QString withForcedSuffix(QString path, const QString &suffix)
{
    const int slash = qMax(path.lastIndexOf(QLatin1Char('/')),
                           path.lastIndexOf(QLatin1Char('\\')));
    const QString dir = slash >= 0 ? path.left(slash + 1) : QString();
    QString name = slash >= 0 ? path.mid(slash + 1) : path;
    name = name.trimmed();
    name.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (name.endsWith(suffix, Qt::CaseInsensitive))
        return dir + name;
    if (suffix.compare(QLatin1String(".pixui.json"), Qt::CaseInsensitive) == 0
        && name.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        name.chop(QLatin1String(".json").size());
    return dir + name + suffix;
}


QImage applyEqFilter(const QImage &src, double contrast, double saturation, int brightness)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double r = qRed(line[x]);
            const double g = qGreen(line[x]);
            const double b = qBlue(line[x]);
            const double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            double rr = lum + (r - lum) * saturation;
            double gg = lum + (g - lum) * saturation;
            double bb = lum + (b - lum) * saturation;
            rr = (rr - 128.0) * contrast + 128.0 + brightness;
            gg = (gg - 128.0) * contrast + 128.0 + brightness;
            bb = (bb - 128.0) * contrast + 128.0 + brightness;
            auto clamp = [](double v) {
                return int(std::lround(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v)));
            };
            line[x] = qRgba(clamp(rr), clamp(gg), clamp(bb), qAlpha(line[x]));
        }
    }
    return img;
}

QByteArray toRgb565(const QImage &src, bool bigEndian)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    QByteArray bytes;
    bytes.reserve(img.width() * img.height() * 2);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int v = ((qRed(line[x]) >> 3) << 11)
                        | ((qGreen(line[x]) >> 2) << 5)
                        | (qBlue(line[x]) >> 3);
            if (bigEndian) {
                bytes.append(char((v >> 8) & 0xff));
                bytes.append(char(v & 0xff));
            } else {
                bytes.append(char(v & 0xff));
                bytes.append(char((v >> 8) & 0xff));
            }
        }
    }
    return bytes;
}

QString toXxdHeader(const QByteArray &bytes, const QString &arrayName,
                    int width, int height, bool bigEndian)
{
    QString out;
    out += QStringLiteral("// Generated by Pixel UI Designer\n");
    out += QStringLiteral("// %1x%2 RGB565%3, %4 bytes\n")
               .arg(width).arg(height)
               .arg(bigEndian ? QStringLiteral("be") : QStringLiteral("le"))
               .arg(bytes.size());
    out += QStringLiteral("#pragma once\n\n");
    out += QStringLiteral("unsigned char %1[] = {\n").arg(arrayName);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i % 12 == 0)
            out += QStringLiteral("  ");
        out += QString::asprintf("0x%02X, ", static_cast<quint8>(bytes.at(i)));
        if (i % 12 == 11 || i == bytes.size() - 1)
            out += QLatin1Char('\n');
    }
    out += QStringLiteral("};\n");
    out += QStringLiteral("unsigned int %1_len = %2;\n").arg(arrayName).arg(bytes.size());
    return out;
}
