#pragma once

#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QQmlEngine;
class QPlainTextEdit;
class QQuickWidget;

/** @brief Исходный текст демонстрационной QML-сцены. */
QString qmlDemoSource();

/** @brief Пост-обработка захваченного кадра QML. */
enum class FrameSnap {
    GrayToBw,  ///< серые пиксели -> чёрный/белый, цвета сохраняются
    Mono1Bit,  ///< весь кадр в 1 бит (эмуляция монохромного дисплея)
    Off        ///< кадр без изменений
};

/**
 * @brief Панель QML-режима: редактор кода и живой предпросмотр сцены.
 *
 * Код пишется во временный файл и по таймеру или F5 перезагружается в
 * QQuickWidget размером с экран; захваченный кадр отдаётся сигналом
 * frameUpdated(). Ошибки компиляции и предупреждения QML попадают в лог.
 */
class QmlPane : public QWidget
{
    Q_OBJECT
public:
    explicit QmlPane(QWidget *parent = nullptr);

    void setScreenSize(int width, int height);
    void setSourceText(const QString &code); ///< задать текст и перезагрузить сцену
    QString source() const;
    QString logText() const;
    QImage lastFrame() const { return m_lastFrame; }

signals:
    void frameUpdated(const QImage &image); ///< новый кадр после фильтрации
    void sourceEdited();

public slots:
    void reloadNow(); ///< записать код во временный файл и перезагрузить сцену

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void appendLog(const QString &line);

    QPlainTextEdit *m_editor = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QQuickWidget *m_quick = nullptr;
    QQmlEngine *m_engine = nullptr;
    QComboBox *m_snapCombo = nullptr; ///< выбор режима FrameSnap
    QTimer m_debounce;                ///< задержка автоперезагрузки при наборе
    QTimer m_grabTimer;               ///< периодический захват кадра
    QTemporaryDir m_tempDir;
    QImage m_lastFrame;
    int m_w = 128;
    int m_h = 160;
    bool m_loading = false; ///< программное изменение редактора, не пользователя
};
