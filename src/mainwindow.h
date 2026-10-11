#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <vector>

#include "screenelement.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QModelIndex;
class PreviewWindow;
class QmlPane;
class ScreenCanvas;

/**
 * @brief The editor main window.
 *
 * Owns the element model and keeps the canvas, layer list, properties
 * panel and the 1:1 preview in sync. Serves two modes: layout editing
 * and QML code editing.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addElement(ElementKind kind);
    void onSelectionChanged();
    void onModelChanged();
    void applyProps();  ///< write properties panel values back to the element
    void onListReordered(const QModelIndex &parent, int start, int end,
                         const QModelIndex &dest, int row);
    void newProject();
    bool saveProject();
    void openProject();
    void exportPng();
    void exportCArray(); ///< RGB565 header generation dialog
    void chooseScreenPreset(int index);
    void setZoom(int zoom);
    void toggleGrid(bool on);
    void togglePreview(bool on);
    void pickFillColor();
    void pickStrokeColor();
    void deleteSelected();
    void duplicateSelected();
    void bringToFront();
    void sendToBack();
    void raiseOne();  ///< raise the selection one layer
    void lowerOne();  ///< lower the selection one layer
    void groupSelected();
    void ungroupSelected();
    void nudgeSelected(int dx, int dy);
    void setQmlMode(bool on);
    void onQmlFrame(const QImage &frame);

private:
    void buildUi();
    void loadDemo();
    void rebuildList();
    void updateListItem(int index);
    void refreshProps();
    void refreshView();
    void refreshTitle();
    void markDirty();
    bool maybeSave(); ///< ask to save when there are unsaved changes
    void applyScreenSize(int width, int height);
    void syncPresetCombo(); ///< select the combo entry for the current screen size
    void syncCounters();    ///< element-name counters, derived from name suffixes
    void setColorButton(QPushButton *button, const QColor &color);
    void moveLayerStep(int dir); ///< move layer up for dir > 0, down for dir < 0
    std::vector<int> selSorted() const;  ///< selected indices, ascending
    int maxGroupId() const;
    static QString itemText(const Element &e);
    static QIcon iconFor(const Element &e);

    std::vector<Element> m_elements; ///< layout model
    int m_screenW = 128;
    int m_screenH = 160;
    QColor m_bgColor = Qt::white;
    bool m_dirty = false;
    QString m_filePath;
    int m_counters[4] = {0, 0, 0, 0}; ///< name counters per element kind
    QImage m_lastRender;              ///< latest frame (export, color picker)
    bool m_updating = false;          ///< re-entrancy guard for UI sync

    ScreenCanvas *m_canvas = nullptr;     ///< layout editing canvas
    ScreenCanvas *m_qmlCanvas = nullptr;  ///< display-only canvas for QML frames
    QTabWidget *m_centralTabs = nullptr;  ///< "Layout" and "QML screen" tabs
    PreviewWindow *m_preview = nullptr;
    QListWidget *m_list = nullptr;      ///< layer list
    QComboBox *m_presetCombo = nullptr; ///< screen size presets
    QSpinBox *m_zoomSpin = nullptr;
    QPushButton *m_bgBtn = nullptr;
    QAction *m_gridAct = nullptr;
    QAction *m_previewAct = nullptr;
    QDockWidget *m_elemDock = nullptr;  ///< layers + properties
    QList<QAction *> m_elementActs;     ///< add-element toolbar actions

    QmlPane *m_qmlPane = nullptr;
    QDockWidget *m_qmlDock = nullptr;
    QAction *m_qmlModeAct = nullptr;
    bool m_qmlMode = false;

    QGroupBox *m_propsBox = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QSpinBox *m_px = nullptr;
    QSpinBox *m_py = nullptr;
    QSpinBox *m_pw = nullptr; ///< width; x2 for Line
    QSpinBox *m_ph = nullptr; ///< height; y2 for Line
    QSpinBox *m_radiusSpin = nullptr;
    QLineEdit *m_textEdit = nullptr;
    QSpinBox *m_fontSpin = nullptr;
    QCheckBox *m_filledCheck = nullptr;
    QPushButton *m_fillBtn = nullptr;
    QPushButton *m_strokeBtn = nullptr;
    QLabel *m_lblW = nullptr;
    QLabel *m_lblH = nullptr;
    QLabel *m_lblRadius = nullptr;
    QLabel *m_lblText = nullptr;
    QLabel *m_lblFont = nullptr;
    QLabel *m_lblFill = nullptr;
    QLabel *m_lblStroke = nullptr;

    QLabel *m_posLabel = nullptr;  ///< cursor position and pixel color
    QLabel *m_sizeLabel = nullptr; ///< screen size
};
