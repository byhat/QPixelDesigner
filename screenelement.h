#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <vector>

/** @brief Тип элемента макета. */
enum class ElementKind {
    Rect,    ///< прямоугольник с опциональным скруглением углов
    Ellipse, ///< эллипс
    Line,    ///< отрезок
    Text     ///< текст
};

QString kindToString(ElementKind kind);           ///< идентификатор типа для JSON
QString kindToLabel(ElementKind kind);            ///< человекочитаемое название типа
ElementKind kindFromString(const QString &s);     ///< обратное к kindToString()

/**
 * @brief Элемент интерфейса в координатах экрана.
 *
 * Вся геометрия задаётся в пикселях проектируемого дисплея, а не в виджетах.
 */
class Element
{
public:
    Element() = default;
    Element(ElementKind kind, const QRect &rect, const QString &name);

    ElementKind kind = ElementKind::Rect; ///< тип элемента
    QString name;                         ///< имя в списке слоёв
    QRect rect;      ///< геометрия; для Line — концы отрезка в topLeft и bottomRight
    QColor fill = Qt::black;   ///< цвет заливки; для Text — цвет текста
    QColor stroke = Qt::black; ///< цвет контура или линии
    bool filled = true;        ///< заливка (true) или только контур 1 px (false)
    QString text;              ///< содержимое элемента Text
    int fontSize = 10;         ///< размер шрифта, px экрана
    int radius = 0;            ///< радиус скругления углов прямоугольника, px
    int group = 0;             ///< id группы; 0 — без группы

    QRect visualRect() const;             ///< занимаемая область с учётом текста
    bool hitTest(const QPointF &p) const; ///< попадание точки в элемент

    QJsonObject toJson() const;                     ///< сериализация в JSON
    static Element fromJson(const QJsonObject &obj); ///< десериализация

private:
    QRect textRect() const; ///< границы текста по метрике шрифта
};

/** @brief Рендер макета в изображение width x height пикселей. */
QImage renderScreen(const std::vector<Element> &elements, int width, int height,
                    const QColor &background);

/**
 * @brief Коррекция изображения, аналог ffmpeg-фильтра eq.
 * @param contrast   множитель контраста
 * @param saturation множитель насыщенности
 * @param brightness сдвиг яркости в уровнях 0..255
 */
QImage applyEqFilter(const QImage &src, double contrast, double saturation, int brightness);

/**
 * @brief Упаковка пикселей в RGB565, по два байта на пиксель.
 * @param bigEndian true — rgb565be, false — rgb565le
 */
QByteArray toRgb565(const QImage &img, bool bigEndian);

/** @brief Заголовок C с массивом байт в формате вывода xxd -i. */
QString toXxdHeader(const QByteArray &bytes, const QString &arrayName,
                    int width, int height, bool bigEndian);

/**
 * @brief Нормализация имени файла при сохранении.
 *
 * Пробелы в имени файла заменяются на '_', пропущенное расширение дописывается
 * ("my ui" -> "my_ui.pixui.json"). Путь к каталогу не изменяется.
 */
QString withForcedSuffix(QString path, const QString &suffix);
