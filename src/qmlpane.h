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

/** @brief Source text of the demo QML scene. */
QString qmlDemoSource();

/** @brief Post-processing applied to captured QML frames. */
enum class FrameSnap {
    GrayToBw,  ///< gray pixels -> black/white, colors preserved
    Mono1Bit,  ///< whole frame to 1 bit (monochrome display emulation)
    Off        ///< frame unchanged
};

/**
 * @brief QML-mode panel: a code editor with a live scene preview.
 *
 * The code is written to a temporary file and reloaded into a QQuickWidget
 * sized to the screen, either debounced while typing or on F5; the captured
 * frame is emitted via frameUpdated(). Compilation errors and QML warnings
 * go to the log.
 */
class QmlPane : public QWidget
{
    Q_OBJECT
public:
    explicit QmlPane(QWidget *parent = nullptr);

    void setScreenSize(int width, int height);
    void setSourceText(const QString &code); ///< set the text and reload the scene
    QString source() const;
    QString logText() const;
    QImage lastFrame() const { return m_lastFrame; }

signals:
    void frameUpdated(const QImage &image); ///< new frame after post-processing
    void sourceEdited();

public slots:
    void reloadNow(); ///< write the code to a temp file and reload the scene

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void appendLog(const QString &line);

    QPlainTextEdit *m_editor = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QQuickWidget *m_quick = nullptr;
    QQmlEngine *m_engine = nullptr;
    QComboBox *m_snapCombo = nullptr; ///< FrameSnap mode selector
    QTimer m_debounce;                ///< typing-to-reload delay
    QTimer m_grabTimer;               ///< periodic frame capture
    QTemporaryDir m_tempDir;
    QImage m_lastFrame;
    int m_w = 128;
    int m_h = 160;
    bool m_loading = false; ///< programmatic editor change, not user input
};
