#pragma once

#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QFileSystemModel;
class QLabel;
class QQmlEngine;
class QPlainTextEdit;
class QQuickWidget;
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
    void newFile();
    void appendLog(const QString &line);

private slots:
    void onTextChanged(); ///< dirty mark on the tab, debounce autosave

private:
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_fsModel = nullptr;
    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QQuickWidget *m_quick = nullptr; ///< hidden offscreen scene renderer
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
