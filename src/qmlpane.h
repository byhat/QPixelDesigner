#pragma once

#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QFileSystemModel;
class QLabel;
class QQmlEngine;
class QPlainTextEdit;
class QQuickWidget;
class QmlFrameView;
class QQuickWindow;
class QTabWidget;
class QTreeView;
class QrcUrlInterceptor;

/** @brief Post-processing applied to captured QML frames. */
enum class FrameSnap {
    GrayToBw,  ///< gray pixels -> black/white, colors preserved
    Mono1Bit,  ///< whole frame to 1 bit (monochrome display emulation)
    Off        ///< frame unchanged
};

/**
 * @brief QML-mode panel: a project tree, a tabbed code editor and a live
 * scene preview.
 *
 * The panel works with a directory on disk. QML files can reference each
 * other by file name; the scene renders from the entry file (main.qml by
 * default, changeable from the tree context menu). Edited files are
 * auto-saved to disk after a debounce and the scene reloads, so changes in
 * any file propagate immediately.
 */
class QmlPane : public QWidget
{
    Q_OBJECT
public:
    explicit QmlPane(QWidget *parent = nullptr);
    ~QmlPane() override;

    void setScreenSize(int width, int height);
    void setActive(bool active); ///< run/pause frame capture for this pane

    bool openProjectDir(const QString &dir, const QString &entry = QString());
    void loadDemoProject(); ///< two-file demo scene in a temporary directory
    bool writeEntrySource(const QString &code); ///< replace entry file contents

    QString projectDir() const { return m_dir; }
    QString entryFile() const { return m_entry; }
    QString logText() const;
    QImage lastFrame() const { return m_lastFrame; }

    /**
     * @brief Stable host widget for the central preview area.
     *
     * Hosts either a QQuickWidget (Item root, e.g. Rectangle) or a window
     * container (ApplicationWindow root); swapped automatically on reload.
     * Shows the raw Qt Quick render: no pixel grid, no post-processing;
     * resized by the owner, keeping the display aspect ratio. The hidden
     * 1:1 renderer stays in the pane and keeps feeding device-true frames.
     */
    QWidget *previewWidget();

signals:
    void frameUpdated(const QImage &image); ///< new frame after post-processing
    void sourceEdited();

public slots:
    void reloadNow(); ///< save the active file and reload the scene

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void openFile(const QString &relPath);
    bool saveFile(int tabIndex);
    void closeTab(int index);
    void setEntry(const QString &relPath);
    void pickDefaultEntry();
    void updateEntryLabel();
    void scanQrc(); ///< rebuild the qrc:/ -> file mapping from *.qrc files
    void rebuildPreview(const QUrl &url); ///< reload the visible preview for both root kinds
    void newFile();
    void appendLog(const QString &line);

private slots:
    void onTextChanged(); ///< dirty mark on the tab, debounce autosave

private:
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_fsModel = nullptr;
    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QQuickWidget *m_quick = nullptr; ///< hidden renderer for Item roots
    QQuickWindow *m_hiddenWindow = nullptr; ///< hidden renderer for ApplicationWindow roots
    QQuickWindow *m_previewWindow = nullptr; ///< hidden renderer feeding the visible preview
    QObject *m_hiddenRoot = nullptr;  ///< component instance owning m_hiddenWindow
    QWidget *m_previewHost = nullptr; ///< stable host, given to the main window
    QmlFrameView *m_previewView = nullptr; ///< paints the preview window frames
    QQmlEngine *m_engine = nullptr;
    QrcUrlInterceptor *m_interceptor = nullptr;
    QComboBox *m_snapCombo = nullptr; ///< FrameSnap mode selector
    QLabel *m_entryLabel = nullptr;   ///< current entry file
    QTimer m_debounce;                ///< typing-to-autosave delay
    QTimer m_grabTimer;               ///< periodic frame capture
    QTemporaryDir m_tempDir;          ///< storage for the demo project
    QString m_dir;                    ///< project directory, absolute
    QString m_entry;                  ///< entry file, relative to m_dir
    QImage m_lastFrame;
    int m_w = 128;
    int m_h = 160;
    int m_lastTabIndex = -1; ///< tab to save when the active tab changes
};
