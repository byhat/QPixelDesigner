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
class QModelIndex;
class PreviewWindow;
class QmlPane;
class ScreenCanvas;

/**
 * @brief Главное окно редактора.
 *
 * Держит модель элементов, синхронизирует холст, список слоёв, панель
 * свойств и окно предпросмотра 1:1. Обслуживает два режима: раскладка
 * готовых элементов и редактирование QML-кода.
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
    void applyProps();  ///< применить значения панели свойств к элементу
    void onListReordered(const QModelIndex &parent, int start, int end,
                         const QModelIndex &dest, int row);
    void newProject();
    bool saveProject();
    void openProject();
    void exportPng();
    void exportCArray(); ///< диалог генерации заголовка с RGB565-массивом
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
    void raiseOne();  ///< поднять выделенные на один слой
    void lowerOne();  ///< опустить выделенные на один слой
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
    bool maybeSave(); ///< запрос сохранения при несохранённых изменениях
    void applyScreenSize(int width, int height);
    void syncPresetCombo(); ///< выбрать в комбобоксе текущий размер экрана
    void syncCounters();    ///< счётчики имён новых элементов по суффиксам
    void setColorButton(QPushButton *button, const QColor &color);
    void moveLayerStep(int dir); ///< сдвиг слоя на dir > 0 вверх, dir < 0 вниз
    std::vector<int> selSorted() const;  ///< выделенные индексы по возрастанию
    int maxGroupId() const;
    static QString itemText(const Element &e);
    static QIcon iconFor(const Element &e);

    std::vector<Element> m_elements; ///< модель макета
    int m_screenW = 128;
    int m_screenH = 160;
    QColor m_bgColor = Qt::white;
    bool m_dirty = false;
    QString m_filePath;
    int m_counters[4] = {0, 0, 0, 0}; ///< счётчики имён по типам элементов
    QImage m_lastRender;              ///< последний кадр (экспорт, пипетка)
    bool m_updating = false;          ///< защита от циклов синхронизации

    ScreenCanvas *m_canvas = nullptr;
    PreviewWindow *m_preview = nullptr;
    QListWidget *m_list = nullptr;      ///< список слоёв
    QComboBox *m_presetCombo = nullptr; ///< пресеты размеров экрана
    QSpinBox *m_zoomSpin = nullptr;
    QPushButton *m_bgBtn = nullptr;
    QAction *m_gridAct = nullptr;
    QAction *m_previewAct = nullptr;
    QDockWidget *m_elemDock = nullptr;  ///< слои + свойства
    QList<QAction *> m_elementActs;     ///< кнопки добавления элементов

    QmlPane *m_qmlPane = nullptr;
    QDockWidget *m_qmlDock = nullptr;
    QAction *m_qmlModeAct = nullptr;
    bool m_qmlMode = false;

    QGroupBox *m_propsBox = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QSpinBox *m_px = nullptr;
    QSpinBox *m_py = nullptr;
    QSpinBox *m_pw = nullptr; ///< ширина; для Line — x2
    QSpinBox *m_ph = nullptr; ///< высота; для Line — y2
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

    QLabel *m_posLabel = nullptr;  ///< координаты и цвет пикселя под курсором
    QLabel *m_sizeLabel = nullptr; ///< размер экрана
};
