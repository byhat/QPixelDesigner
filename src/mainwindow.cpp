#include "mainwindow.h"

#include "previewwindow.h"
#include "qmlpane.h"

#include <QQuickWidget>
#include "screencanvas.h"

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    loadDemo();
    refreshTitle();
    refreshView();
}

void MainWindow::buildUi()
{
    m_canvas = new ScreenCanvas;
    m_canvas->setModel(&m_elements);
    m_canvas->setBackgroundColor(m_bgColor);
    m_canvas->resize(m_canvas->sizeHint());

    auto *scroll = new QScrollArea;
    scroll->setWidget(m_canvas);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);

    m_qmlCanvas = new ScreenCanvas; // raster QML frames with the pixel grid
    m_qmlCanvas->setScreenSize(m_screenW, m_screenH);
    m_qmlCanvas->resize(m_qmlCanvas->sizeHint());
    auto *qmlPixelsScroll = new QScrollArea;
    qmlPixelsScroll->setWidget(m_qmlCanvas);
    qmlPixelsScroll->setWidgetResizable(false);
    qmlPixelsScroll->setAlignment(Qt::AlignCenter);

    m_stack = new QStackedWidget;
    m_stack->addWidget(scroll);           // page 0: layout editor
    m_stack->addWidget(qmlPixelsScroll);  // page 1: pixel-true QML view

    m_qmlScroll = new QScrollArea;
    m_qmlScroll->setWidgetResizable(false);
    m_qmlScroll->setAlignment(Qt::AlignCenter);
    m_qmlScroll->viewport()->installEventFilter(this);

    m_centralTabs = new QTabWidget;
    m_centralTabs->addTab(m_stack, tr("Layout"));
    m_centralTabs->addTab(m_qmlScroll, tr("QML screen"));
    m_centralTabs->setTabEnabled(1, false);
    connect(m_centralTabs, &QTabWidget::currentChanged, this, &MainWindow::onPreviewTabChanged);
    setCentralWidget(m_centralTabs);

    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);

    m_presetCombo = new QComboBox;
    m_presetCombo->addItems({QStringLiteral("128 × 160 (ST7735)"),
                             QStringLiteral("160 × 128"),
                             QStringLiteral("128 × 64 (SSD1306)"),
                             QStringLiteral("320 × 240 (ILI9341)"),
                             tr("Custom size…")});
    connect(m_presetCombo, &QComboBox::activated, this, &MainWindow::chooseScreenPreset);
    tb->addWidget(m_presetCombo);

    m_bgBtn = new QPushButton(tr("Background…"));
    connect(m_bgBtn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_bgColor, this, tr("Screen background color"));
        if (!c.isValid())
            return;
        m_bgColor = c;
        m_canvas->setBackgroundColor(c);
        setColorButton(m_bgBtn, c);
        markDirty();
        refreshView();
    });
    tb->addWidget(m_bgBtn);

    tb->addSeparator();
    tb->addWidget(new QLabel(tr("Zoom:")));
    m_zoomSpin = new QSpinBox;
    m_zoomSpin->setRange(1, 24);
    m_zoomSpin->setValue(m_canvas->zoom());
    connect(m_zoomSpin, &QSpinBox::valueChanged, this, &MainWindow::setZoom);
    tb->addWidget(m_zoomSpin);

    m_gridAct = new QAction(tr("Grid"), this);
    m_gridAct->setCheckable(true);
    m_gridAct->setChecked(true);
    connect(m_gridAct, &QAction::toggled, this, &MainWindow::toggleGrid);
    tb->addAction(m_gridAct);

    m_previewAct = new QAction(tr("Preview 1:1"), this);
    m_previewAct->setCheckable(true);
    m_previewAct->setChecked(true);
    connect(m_previewAct, &QAction::toggled, this, &MainWindow::togglePreview);
    tb->addAction(m_previewAct);

    tb->addSeparator();
    const QList<QPair<QString, ElementKind>> adds = {
        {tr("+ Rectangle"), ElementKind::Rect},
        {tr("+ Ellipse"), ElementKind::Ellipse},
        {tr("+ Line"), ElementKind::Line},
        {tr("+ Text"), ElementKind::Text},
    };
    for (const auto &a : adds) {
        auto *act = new QAction(a.first, this);
        const ElementKind k = a.second;
        connect(act, &QAction::triggered, this, [this, k] { addElement(k); });
        m_elementActs.append(act);
        tb->addAction(act);
    }

    tb->addSeparator();
    m_qmlModeAct = new QAction(tr("QML mode"), this);
    m_qmlModeAct->setCheckable(true);
    m_qmlModeAct->setToolTip(
        tr("Write QML in an IDE-like editor and see the result on the pixel screen"));
    connect(m_qmlModeAct, &QAction::toggled, this, &MainWindow::setQmlMode);
    tb->addAction(m_qmlModeAct);

    tb->addSeparator();
    auto *aNew = new QAction(tr("New"), this);
    connect(aNew, &QAction::triggered, this, &MainWindow::newProject);
    tb->addAction(aNew);
    auto *aOpen = new QAction(tr("Open…"), this);
    connect(aOpen, &QAction::triggered, this, &MainWindow::openProject);
    tb->addAction(aOpen);
    auto *aSave = new QAction(tr("Save…"), this);
    connect(aSave, &QAction::triggered, this, [this] { saveProject(); });
    tb->addAction(aSave);
    auto *aPng = new QAction(tr("Export PNG"), this);
    connect(aPng, &QAction::triggered, this, &MainWindow::exportPng);
    tb->addAction(aPng);
    auto *aC = new QAction(tr("RGB565 C array…"), this);
    connect(aC, &QAction::triggered, this, &MainWindow::exportCArray);
    tb->addAction(aC);

    auto *dock = new QDockWidget(tr("Layers and properties"), this);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    auto *panel = new QWidget;
    auto *panelLay = new QVBoxLayout(panel);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved,
            this, &MainWindow::onListReordered);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_updating)
            return;
        QSet<int> sel;
        const auto items = m_list->selectedItems();
        for (QListWidgetItem *it : items)
            sel.insert(it->data(Qt::UserRole).toInt());
        m_canvas->setSelection(sel);
        refreshProps();
    });
    panelLay->addWidget(m_list, 1);

    auto *layerRow = new QHBoxLayout;
    auto *bUp = new QPushButton(tr("Up"));
    auto *bDown = new QPushButton(tr("Down"));
    auto *bGroup = new QPushButton(tr("Group"));
    auto *bUngroup = new QPushButton(tr("Ungroup"));
    bUp->setToolTip(tr("Raise selection one layer up (PgUp)"));
    bDown->setToolTip(tr("Lower selection one layer down (PgDn)"));
    bGroup->setToolTip(tr("Group selection (Ctrl+G): elements move together"));
    bUngroup->setToolTip(tr("Ungroup (Ctrl+Shift+G)"));
    connect(bUp, &QPushButton::clicked, this, &MainWindow::raiseOne);
    connect(bDown, &QPushButton::clicked, this, &MainWindow::lowerOne);
    connect(bGroup, &QPushButton::clicked, this, &MainWindow::groupSelected);
    connect(bUngroup, &QPushButton::clicked, this, &MainWindow::ungroupSelected);
    layerRow->addWidget(bUp);
    layerRow->addWidget(bDown);
    layerRow->addWidget(bGroup);
    layerRow->addWidget(bUngroup);
    panelLay->addLayout(layerRow);

    m_propsBox = new QGroupBox(tr("Properties"));
    auto *grid = new QGridLayout(m_propsBox);
    int row = 0;

    auto *lblName = new QLabel(tr("Name:"));
    m_nameEdit = new QLineEdit;
    grid->addWidget(lblName, row, 0);
    grid->addWidget(m_nameEdit, row, 1);
    ++row;

    grid->addWidget(new QLabel("x:"), row, 0);
    m_px = new QSpinBox;
    m_px->setRange(-1024, 4095);
    grid->addWidget(m_px, row, 1);
    ++row;

    grid->addWidget(new QLabel("y:"), row, 0);
    m_py = new QSpinBox;
    m_py->setRange(-1024, 4095);
    grid->addWidget(m_py, row, 1);
    ++row;

    m_lblW = new QLabel(tr("Width:"));
    m_pw = new QSpinBox;
    m_pw->setRange(1, 4096);
    grid->addWidget(m_lblW, row, 0);
    grid->addWidget(m_pw, row, 1);
    ++row;

    m_lblH = new QLabel(tr("Height:"));
    m_ph = new QSpinBox;
    m_ph->setRange(1, 4096);
    grid->addWidget(m_lblH, row, 0);
    grid->addWidget(m_ph, row, 1);
    ++row;

    m_lblRadius = new QLabel(tr("Corner radius:"));
    m_radiusSpin = new QSpinBox;
    m_radiusSpin->setRange(0, 500);
    m_radiusSpin->setToolTip(tr("Rectangle corner radius, in screen pixels"));
    grid->addWidget(m_lblRadius, row, 0);
    grid->addWidget(m_radiusSpin, row, 1);
    ++row;

    m_lblText = new QLabel(tr("Text:"));
    m_textEdit = new QLineEdit;
    grid->addWidget(m_lblText, row, 0);
    grid->addWidget(m_textEdit, row, 1);
    ++row;

    m_lblFont = new QLabel(tr("Font size:"));
    m_fontSpin = new QSpinBox;
    m_fontSpin->setRange(4, 96);
    grid->addWidget(m_lblFont, row, 0);
    grid->addWidget(m_fontSpin, row, 1);
    ++row;

    m_filledCheck = new QCheckBox(tr("Filled (otherwise outline only)"));
    grid->addWidget(m_filledCheck, row, 0, 1, 2);
    ++row;

    m_lblFill = new QLabel(tr("Fill color:"));
    m_fillBtn = new QPushButton;
    grid->addWidget(m_lblFill, row, 0);
    grid->addWidget(m_fillBtn, row, 1);
    ++row;

    m_lblStroke = new QLabel(tr("Outline color:"));
    m_strokeBtn = new QPushButton;
    grid->addWidget(m_lblStroke, row, 0);
    grid->addWidget(m_strokeBtn, row, 1);
    ++row;

    panelLay->addWidget(m_propsBox);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    m_elemDock = dock;

    m_qmlPane = new QmlPane;
    m_qmlPane->setScreenSize(m_screenW, m_screenH);
    m_qmlPane->loadDemoProject();
    connect(m_qmlPane, &QmlPane::frameUpdated, this, &MainWindow::onQmlFrame);
    connect(m_qmlPane, &QmlPane::sourceEdited, this, &MainWindow::markDirty);
    connect(m_qmlPane, &QmlPane::varsEdited, this, &MainWindow::markDirty);

    m_qmlScroll->setWidget(m_qmlPane->previewWidget());
    QTimer::singleShot(0, this, &MainWindow::fitQmlPreview);

    m_qmlDock = new QDockWidget(tr("QML editor"), this);
    m_qmlDock->setWidget(m_qmlPane);
    addDockWidget(Qt::LeftDockWidgetArea, m_qmlDock);
    m_qmlDock->hide();

    connect(m_nameEdit, &QLineEdit::textChanged, this, &MainWindow::applyProps);
    connect(m_textEdit, &QLineEdit::textChanged, this, &MainWindow::applyProps);
    connect(m_px, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_py, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_pw, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_ph, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_radiusSpin, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_fontSpin, &QSpinBox::valueChanged, this, &MainWindow::applyProps);
    connect(m_filledCheck, &QCheckBox::toggled, this, &MainWindow::applyProps);
    connect(m_fillBtn, &QPushButton::clicked, this, &MainWindow::pickFillColor);
    connect(m_strokeBtn, &QPushButton::clicked, this, &MainWindow::pickStrokeColor);

    connect(m_canvas, &ScreenCanvas::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_canvas, &ScreenCanvas::modelChanged, this, &MainWindow::onModelChanged);
    connect(m_canvas, &ScreenCanvas::zoomChanged, this, &MainWindow::setZoom);
    connect(m_canvas, &ScreenCanvas::hoverChanged, this, [this](const QPoint &p) {
        QString s = tr("off-screen");
        if (p.x() >= 0 && p.x() < m_screenW && p.y() >= 0 && p.y() < m_screenH) {
            if (m_lastRender.size() == QSize(m_screenW, m_screenH)) {
                const QColor c(m_lastRender.pixel(p.x(), p.y()));
                s = tr("x: %1  y: %2  color: %3").arg(p.x()).arg(p.y()).arg(c.name().toUpper());
            } else {
                s = tr("x: %1  y: %2").arg(p.x()).arg(p.y());
            }
        }
        m_posLabel->setText(s);
    });

    m_canvas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_canvas, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (m_canvas->selectionCount() == 0)
            return;
        QMenu menu;
        menu.addAction(tr("Bring to front"), this, &MainWindow::bringToFront);
        menu.addAction(tr("Raise one layer"), this, &MainWindow::raiseOne);
        menu.addAction(tr("Lower one layer"), this, &MainWindow::lowerOne);
        menu.addAction(tr("Send to back"), this, &MainWindow::sendToBack);
        menu.addSeparator();
        menu.addAction(tr("Group"), this, &MainWindow::groupSelected);
        menu.addAction(tr("Ungroup"), this, &MainWindow::ungroupSelected);
        menu.addSeparator();
        menu.addAction(tr("Duplicate"), this, &MainWindow::duplicateSelected);
        menu.addAction(tr("Delete"), this, &MainWindow::deleteSelected);
        menu.exec(m_canvas->mapToGlobal(pos));
    });

    auto addShortcut = [this](const QString &keys, const std::function<void()> &fn) {
        auto *s = new QShortcut(QKeySequence(keys), m_canvas);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    addShortcut(QStringLiteral("Delete"), [this] { deleteSelected(); });
    addShortcut(QStringLiteral("Backspace"), [this] { deleteSelected(); });
    addShortcut(QStringLiteral("Ctrl+D"), [this] { duplicateSelected(); });
    addShortcut(QStringLiteral("Ctrl+G"), [this] { groupSelected(); });
    addShortcut(QStringLiteral("Ctrl+Shift+G"), [this] { ungroupSelected(); });
    addShortcut(QStringLiteral("PgUp"), [this] { raiseOne(); });
    addShortcut(QStringLiteral("PgDown"), [this] { lowerOne(); });
    addShortcut(QStringLiteral("Ctrl+A"), [this] {
        QSet<int> all;
        for (int i = 0; i < int(m_elements.size()); ++i)
            all.insert(i);
        m_canvas->setSelection(all);
    });
    addShortcut(QStringLiteral("Left"), [this] { nudgeSelected(-1, 0); });
    addShortcut(QStringLiteral("Right"), [this] { nudgeSelected(1, 0); });
    addShortcut(QStringLiteral("Up"), [this] { nudgeSelected(0, -1); });
    addShortcut(QStringLiteral("Down"), [this] { nudgeSelected(0, 1); });
    addShortcut(QStringLiteral("Shift+Left"), [this] { nudgeSelected(-5, 0); });
    addShortcut(QStringLiteral("Shift+Right"), [this] { nudgeSelected(5, 0); });
    addShortcut(QStringLiteral("Shift+Up"), [this] { nudgeSelected(0, -5); });
    addShortcut(QStringLiteral("Shift+Down"), [this] { nudgeSelected(0, 5); });

    m_posLabel = new QLabel(QStringLiteral("x: —  y: —"));
    m_sizeLabel = new QLabel;
    statusBar()->addWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_sizeLabel);
    m_sizeLabel->setText(tr("Screen: %1x%2 px").arg(m_screenW).arg(m_screenH));

    setColorButton(m_bgBtn, m_bgColor);
    syncPresetCombo();
}

void MainWindow::loadDemo()
{
    m_elements.clear();

    Element title(ElementKind::Text, QRect(6, 6, 10, 10), QStringLiteral("title"));
    title.text = QStringLiteral("THERMOSTAT");
    title.fontSize = 11;

    Element divider(ElementKind::Line, QRect(6, 22, 116, 0), QStringLiteral("divider"));

    Element item1(ElementKind::Rect, QRect(6, 30, 116, 20), QStringLiteral("item1"));
    item1.filled = false;
    item1.radius = 4;
    item1.group = 1;
    Element item1Text(ElementKind::Text, QRect(12, 34, 10, 10), QStringLiteral("item1_text"));
    item1Text.text = QStringLiteral("1. Home");
    item1Text.fontSize = 10;
    item1Text.group = 1;

    Element item2(ElementKind::Rect, QRect(6, 54, 116, 20), QStringLiteral("item2"));
    item2.filled = false;
    item2.radius = 4;
    item2.group = 2;
    Element item2Text(ElementKind::Text, QRect(12, 58, 10, 10), QStringLiteral("item2_text"));
    item2Text.text = QStringLiteral("2. Settings");
    item2Text.fontSize = 10;
    item2Text.group = 2;

    Element okBtn(ElementKind::Rect, QRect(6, 130, 116, 18), QStringLiteral("ok_button"));
    okBtn.radius = 6;
    Element okText(ElementKind::Text, QRect(57, 134, 10, 10), QStringLiteral("ok_text"));
    okText.text = QStringLiteral("OK");
    okText.fontSize = 10;
    okText.fill = Qt::white;

    m_elements = {title, divider, item1, item1Text, item2, item2Text, okBtn, okText};
    syncCounters();
    m_canvas->clearSelection();
    rebuildList();
    refreshProps();
    m_lastRender = renderScreen(m_elements, m_screenW, m_screenH, m_bgColor);
}


std::vector<int> MainWindow::selSorted() const
{
    const QSet<int> s = m_canvas->selection();
    std::vector<int> v(s.cbegin(), s.cend());
    std::sort(v.begin(), v.end());
    return v;
}

int MainWindow::maxGroupId() const
{
    int g = 0;
    for (const Element &e : m_elements)
        g = qMax(g, e.group);
    return g;
}

void MainWindow::onSelectionChanged()
{
    if (m_updating)
        return;
    m_updating = true;
    const QSet<int> sel = m_canvas->selection();
    for (int r = 0; r < m_list->count(); ++r)
        m_list->item(r)->setSelected(sel.contains(r));
    m_updating = false;
    refreshProps();
}


void MainWindow::addElement(ElementKind kind)
{
    static const char *names[] = {"rect", "ellipse", "line", "text"};
    const int idx = int(kind);
    m_counters[idx]++;

    Element e;
    e.kind = kind;
    e.name = QString("%1_%2").arg(names[idx]).arg(m_counters[idx]);
    switch (kind) {
    case ElementKind::Rect:
        e.rect = QRect(8, 8, 64, 24);
        e.radius = 4;
        break;
    case ElementKind::Ellipse:
        e.rect = QRect(16, 16, 48, 32);
        break;
    case ElementKind::Line:
        e.rect = QRect(10, 10, 60, 30);
        break;
    case ElementKind::Text:
        e.rect = QRect(8, 8, 10, 10);
        e.text = tr("Text");
        e.fontSize = 12;
        break;
    }

    m_elements.push_back(e);
    m_canvas->setSelection(QSet<int>{int(m_elements.size()) - 1});
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::deleteSelected()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    for (int i = int(sel.size()) - 1; i >= 0; --i)
        m_elements.erase(m_elements.begin() + sel[i]);
    m_canvas->clearSelection();
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::duplicateSelected()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    const QSet<int> selSet(sel.cbegin(), sel.cend());

    auto wholeGroupSelected = [&](int g) {
        for (int j = 0; j < int(m_elements.size()); ++j)
            if (m_elements[j].group == g && !selSet.contains(j))
                return false;
        return true;
    };

    std::vector<Element> copies;
    for (int i : sel) {
        Element e = m_elements[i];
        e.name += "_copy";
        e.rect.translate(4, 4);
        if (e.group > 0 && !wholeGroupSelected(e.group))
            e.group = 0;
        copies.push_back(e);
    }

    const int at = sel.back() + 1;
    m_elements.insert(m_elements.begin() + at, copies.begin(), copies.end());
    QSet<int> ns;
    for (int k = 0; k < int(copies.size()); ++k)
        ns.insert(at + k);
    m_canvas->setSelection(ns);
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::bringToFront()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    const QSet<int> selSet(sel.cbegin(), sel.cend());
    std::vector<Element> rest, front;
    for (int i = 0; i < int(m_elements.size()); ++i)
        (selSet.contains(i) ? front : rest).push_back(m_elements[i]);
    const int restN = int(rest.size());
    m_elements = rest;
    m_elements.insert(m_elements.end(), front.begin(), front.end());

    QSet<int> ns;
    for (int i = restN; i < int(m_elements.size()); ++i)
        ns.insert(i);
    m_canvas->setSelection(ns);
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::sendToBack()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    const QSet<int> selSet(sel.cbegin(), sel.cend());
    std::vector<Element> back, rest;
    for (int i = 0; i < int(m_elements.size()); ++i)
        (selSet.contains(i) ? back : rest).push_back(m_elements[i]);
    m_elements = back;
    m_elements.insert(m_elements.end(), rest.begin(), rest.end());

    QSet<int> ns;
    for (int i = 0; i < int(back.size()); ++i)
        ns.insert(i);
    m_canvas->setSelection(ns);
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::moveLayerStep(int dir)
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    const QSet<int> s(sel.cbegin(), sel.cend());
    const int n = int(m_elements.size());
    std::vector<int> ids(n);
    for (int i = 0; i < n; ++i)
        ids[i] = i;

    if (dir > 0) {
        for (int i = n - 2; i >= 0; --i) {
            if (s.contains(ids[i]) && !s.contains(ids[i + 1])) {
                std::swap(m_elements[i], m_elements[i + 1]);
                std::swap(ids[i], ids[i + 1]);
            }
        }
    } else {
        for (int i = 1; i < n; ++i) {
            if (s.contains(ids[i]) && !s.contains(ids[i - 1])) {
                std::swap(m_elements[i], m_elements[i - 1]);
                std::swap(ids[i], ids[i - 1]);
            }
        }
    }

    QSet<int> ns;
    for (int i = 0; i < n; ++i)
        if (s.contains(ids[i]))
            ns.insert(i);
    m_canvas->setSelection(ns);
    rebuildList();
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::raiseOne() { moveLayerStep(1); }
void MainWindow::lowerOne() { moveLayerStep(-1); }

void MainWindow::groupSelected()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    const int gid = maxGroupId() + 1;
    for (int i : sel)
        m_elements[i].group = gid;
    rebuildList();
    markDirty();
    refreshView();
}

void MainWindow::ungroupSelected()
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    bool changed = false;
    for (int i : sel) {
        if (m_elements[i].group != 0) {
            m_elements[i].group = 0;
            changed = true;
        }
    }
    if (!changed)
        return;
    rebuildList();
    markDirty();
    refreshView();
}

void MainWindow::nudgeSelected(int dx, int dy)
{
    const std::vector<int> sel = selSorted();
    if (sel.empty())
        return;
    QSet<int> move(sel.cbegin(), sel.cend());
    for (int i : sel) {
        const int g = m_elements[i].group;
        if (g > 0) {
            for (int j = 0; j < int(m_elements.size()); ++j)
                if (m_elements[j].group == g)
                    move.insert(j);
        }
    }
    for (int i : move)
        m_elements[i].rect.translate(dx, dy);
    refreshProps();
    markDirty();
    refreshView();
}


QString MainWindow::itemText(const Element &e)
{
    QString t = QStringLiteral("%1 — %2").arg(e.name, kindToLabel(e.kind));
    if (e.group > 0)
        t += QStringLiteral("  [G%1]").arg(e.group);
    return t;
}

QIcon MainWindow::iconFor(const Element &e)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (e.kind == ElementKind::Line) {
        p.setPen(QPen(e.stroke, 3));
        p.drawLine(2, 13, 13, 2);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(e.fill);
        p.drawRoundedRect(2, 2, 12, 12, 3, 3);
    }
    return QIcon(pm);
}

void MainWindow::rebuildList()
{
    m_updating = true;
    m_list->clear();
    const QSet<int> sel = m_canvas->selection();
    for (size_t i = 0; i < m_elements.size(); ++i) {
        auto *item = new QListWidgetItem(iconFor(m_elements[i]), itemText(m_elements[i]));
        item->setData(Qt::UserRole, int(i));
        item->setSelected(sel.contains(int(i)));
        m_list->addItem(item);
    }
    m_updating = false;
}

void MainWindow::updateListItem(int index)
{
    if (index < 0 || index >= int(m_elements.size()) || index >= m_list->count())
        return;
    m_list->item(index)->setText(itemText(m_elements[index]));
    m_list->item(index)->setIcon(iconFor(m_elements[index]));
}

void MainWindow::onListReordered(const QModelIndex &parent, int start, int end,
                                 const QModelIndex &dest, int row)
{
    Q_UNUSED(parent);
    Q_UNUSED(start);
    Q_UNUSED(end);
    Q_UNUSED(dest);
    Q_UNUSED(row);
    if (!m_list || m_list->count() != int(m_elements.size()))
        return;

    std::vector<int> order;
    order.reserve(m_elements.size());
    for (int r = 0; r < m_list->count(); ++r) {
        const int idx = m_list->item(r)->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= int(m_elements.size()))
            return;
        order.push_back(idx);
    }
    QSet<int> sel;
    const auto items = m_list->selectedItems();
    for (QListWidgetItem *it : items)
        sel.insert(it->data(Qt::UserRole).toInt());

    QTimer::singleShot(0, this, [this, order, sel] {
        const std::vector<Element> old = m_elements;
        std::vector<Element> next;
        next.reserve(old.size());
        for (int idx : order)
            next.push_back(old[idx]);
        m_elements = next;
        m_canvas->setSelection(sel);
        rebuildList();
        refreshProps();
        markDirty();
        refreshView();
    });
}

void MainWindow::refreshProps()
{
    m_updating = true;
    const int n = m_canvas->selectionCount();
    const int i = n == 1 ? m_canvas->primaryIndex() : -1;
    m_propsBox->setEnabled(n == 1);
    m_propsBox->setTitle(n == 1 ? tr("Properties")
                                : tr("Properties (selected: %1)").arg(n));
    if (i < 0) {
        m_updating = false;
        return;
    }

    const Element &e = m_elements[i];
    m_nameEdit->setText(e.name);
    m_px->setValue(e.rect.x());
    m_py->setValue(e.rect.y());
    if (e.kind == ElementKind::Line) {
        m_lblW->setText("x2:");
        m_lblH->setText("y2:");
        m_pw->setMinimum(-1024);
        m_pw->setMaximum(4095);
        m_ph->setMinimum(-1024);
        m_ph->setMaximum(4095);
        m_pw->setValue(e.rect.right());
        m_ph->setValue(e.rect.bottom());
    } else {
        m_lblW->setText(tr("Width:"));
        m_lblH->setText(tr("Height:"));
        m_pw->setMinimum(1);
        m_pw->setMaximum(4096);
        m_ph->setMinimum(1);
        m_ph->setMaximum(4096);
        m_pw->setValue(e.rect.width());
        m_ph->setValue(e.rect.height());
    }

    const bool isText = e.kind == ElementKind::Text;
    const bool isLine = e.kind == ElementKind::Line;
    const bool isRect = e.kind == ElementKind::Rect;

    m_lblRadius->setVisible(isRect);
    m_radiusSpin->setVisible(isRect);
    m_radiusSpin->setValue(e.radius);

    m_lblText->setVisible(isText);
    m_textEdit->setVisible(isText);
    m_lblFont->setVisible(isText);
    m_fontSpin->setVisible(isText);
    m_textEdit->setText(e.text);
    m_fontSpin->setValue(e.fontSize);

    m_filledCheck->setVisible(!isText && !isLine);
    m_filledCheck->setChecked(e.filled);

    m_lblFill->setVisible(!isLine);
    m_fillBtn->setVisible(!isLine);
    m_lblFill->setText(isText ? tr("Text color:") : tr("Fill color:"));
    setColorButton(m_fillBtn, e.fill);

    m_lblStroke->setVisible(!isText);
    m_strokeBtn->setVisible(!isText);
    m_lblStroke->setText(isLine ? tr("Line color:") : tr("Outline color:"));
    setColorButton(m_strokeBtn, e.stroke);

    m_lblW->setVisible(!isText);
    m_pw->setVisible(!isText);
    m_lblH->setVisible(!isText);
    m_ph->setVisible(!isText);
    m_updating = false;
}

void MainWindow::applyProps()
{
    if (m_updating)
        return;
    const int i = m_canvas->primaryIndex();
    if (i < 0 || i >= int(m_elements.size()))
        return;

    Element &e = m_elements[i];
    e.name = m_nameEdit->text();
    e.rect.setX(m_px->value());
    e.rect.setY(m_py->value());
    if (e.kind == ElementKind::Line) {
        e.rect.setRight(m_pw->value());
        e.rect.setBottom(m_ph->value());
    } else {
        e.rect.setWidth(m_pw->value());
        e.rect.setHeight(m_ph->value());
    }
    e.radius = m_radiusSpin->value();
    e.text = m_textEdit->text();
    e.fontSize = m_fontSpin->value();
    e.filled = m_filledCheck->isChecked();

    updateListItem(i);
    markDirty();
    refreshView();
}

void MainWindow::onModelChanged()
{
    refreshProps();
    markDirty();
    refreshView();
}

void MainWindow::refreshView()
{
    m_canvas->update();
    if (m_qmlMode) {
        m_lastRender = m_qmlFrame;
        m_qmlCanvas->setExternalImage(m_qmlFrame);
        if (m_preview)
            m_preview->setImage(m_qmlFrame);
    } else {
        m_lastRender = renderScreen(m_elements, m_screenW, m_screenH, m_bgColor);
        if (m_preview)
            m_preview->setImage(m_lastRender);
    }
}


void MainWindow::setQmlMode(bool on)
{
    if (m_qmlMode == on)
        return;
    m_qmlMode = on;

    // QML-режим полностью переключает работу на QML-проект: редактор
    // макета (док слоёв, кнопки элементов, фон) прячется до возврата
    m_qmlDock->setVisible(on);
    m_elemDock->setVisible(!on);
    for (QAction *a : m_elementActs)
        a->setEnabled(!on);
    m_bgBtn->setEnabled(!on);
    m_qmlPane->setActive(on);
    m_qmlPane->setScreenSize(m_screenW, m_screenH);
    m_stack->setCurrentIndex(on ? 1 : 0);
    m_centralTabs->setTabEnabled(1, on);
    // переход в режим заменяет центральное превью на QML-превью
    m_centralTabs->setCurrentIndex(on ? 1 : 0);
    QTimer::singleShot(0, this, &MainWindow::fitQmlPreview);
}

void MainWindow::onQmlFrame(const QImage &frame)
{
    if (!m_qmlMode)
        return;
    m_qmlFrame = frame;
    m_lastRender = frame;
    m_qmlCanvas->setExternalImage(frame);
    if (m_preview)
        m_preview->setImage(frame);
}

void MainWindow::onPreviewTabChanged(int index)
{
    if (index == 1)
        QTimer::singleShot(0, this, &MainWindow::fitQmlPreview);
    if (m_qmlMode)
        m_lastRender = m_qmlFrame;
    else
        m_lastRender = renderScreen(m_elements, m_screenW, m_screenH, m_bgColor);
    if (m_preview)
        m_preview->setImage(m_lastRender);
}

void MainWindow::refreshTitle()
{
    const QString name = m_filePath.isEmpty() ? tr("untitled") : QFileInfo(m_filePath).fileName();
    setWindowTitle(QStringLiteral("%1%2 — QPixelDesigner")
                       .arg(m_dirty ? "*" : "", name));
}

void MainWindow::markDirty()
{
    m_dirty = true;
    refreshTitle();
}


void MainWindow::applyScreenSize(int width, int height)
{
    m_screenW = width;
    m_screenH = height;
    m_canvas->setScreenSize(width, height);
    m_qmlCanvas->setScreenSize(width, height);
    m_qmlPane->setScreenSize(width, height);
    QTimer::singleShot(0, this, &MainWindow::fitQmlPreview);
    m_sizeLabel->setText(tr("Screen: %1x%2 px").arg(m_screenW).arg(m_screenH));
    syncPresetCombo();
}

void MainWindow::syncPresetCombo()
{
    int idx = 4;
    if (m_screenW == 128 && m_screenH == 160)      idx = 0;
    else if (m_screenW == 160 && m_screenH == 128) idx = 1;
    else if (m_screenW == 128 && m_screenH == 64)  idx = 2;
    else if (m_screenW == 320 && m_screenH == 240) idx = 3;
    m_presetCombo->setCurrentIndex(idx);
}

void MainWindow::chooseScreenPreset(int index)
{
    switch (index) {
    case 0: applyScreenSize(128, 160); break;
    case 1: applyScreenSize(160, 128); break;
    case 2: applyScreenSize(128, 64);  break;
    case 3: applyScreenSize(320, 240); break;
    case 4: {
        const QString txt = QInputDialog::getText(
            this, tr("Custom screen size"),
            tr("Format WIDTHxHEIGHT (e.g. 128x160):"),
            QLineEdit::Normal, QString("%1x%2").arg(m_screenW).arg(m_screenH));
        if (!txt.isEmpty()) {
            static const QRegularExpression re("^\\s*(\\d{1,4})\\s*[xX×*]\\s*(\\d{1,4})\\s*$");
            const auto m = re.match(txt);
            if (m.hasMatch()) {
                applyScreenSize(qBound(8, m.captured(1).toInt(), 2048),
                                qBound(8, m.captured(2).toInt(), 2048));
            } else {
                QMessageBox::warning(this, tr("QPixelDesigner"),
                                     tr("Cannot parse size: %1").arg(txt));
            }
        }
        syncPresetCombo();
        break;
    }
    }
    markDirty();
    refreshView();
}

void MainWindow::setZoom(int zoom)
{
    zoom = qBound(1, zoom, 24);
    if (zoom != m_canvas->zoom())
        m_canvas->setZoom(zoom);
    if (zoom != m_qmlCanvas->zoom())
        m_qmlCanvas->setZoom(zoom);
    if (m_zoomSpin->value() != zoom)
        m_zoomSpin->setValue(zoom);
}

void MainWindow::fitQmlPreview()
{
    if (!m_qmlPane)
        return;
    const QSize vp = m_qmlScroll->viewport()->size();
    if (vp.isEmpty())
        return;
    const double s = qMin(double(vp.width()) / m_screenW, double(vp.height()) / m_screenH);
    m_qmlPane->previewWidget()->setFixedSize(qMax(1, int(m_screenW * s)),
                                             qMax(1, int(m_screenH * s)));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_qmlScroll->viewport() && event->type() == QEvent::Resize)
        fitQmlPreview();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleGrid(bool on)
{
    m_canvas->setShowGrid(on);
    m_qmlCanvas->setShowGrid(on);
}

void MainWindow::togglePreview(bool on)
{
    if (on) {
        if (!m_preview) {
            m_preview = new PreviewWindow(this);
            connect(m_preview, &PreviewWindow::hidden, this,
                    [this] { m_previewAct->setChecked(false); });
        }
        m_preview->setImage(m_lastRender);
        m_preview->show();
    } else if (m_preview) {
        m_preview->hide();
    }
}


void MainWindow::setColorButton(QPushButton *button, const QColor &color)
{
    button->setText(color.name().toUpper());
    button->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold;")
                              .arg(color.name())
                              .arg(color.lightness() > 128 ? "black" : "white"));
}

void MainWindow::pickFillColor()
{
    const int i = m_canvas->primaryIndex();
    if (i < 0 || i >= int(m_elements.size()))
        return;
    const QColor c = QColorDialog::getColor(m_elements[i].fill, this, tr("Fill color"));
    if (!c.isValid())
        return;
    m_elements[i].fill = c;
    setColorButton(m_fillBtn, c);
    updateListItem(i);
    markDirty();
    refreshView();
}

void MainWindow::pickStrokeColor()
{
    const int i = m_canvas->primaryIndex();
    if (i < 0 || i >= int(m_elements.size()))
        return;
    const QColor c = QColorDialog::getColor(m_elements[i].stroke, this, tr("Outline color"));
    if (!c.isValid())
        return;
    m_elements[i].stroke = c;
    setColorButton(m_strokeBtn, c);
    updateListItem(i);
    markDirty();
    refreshView();
}


void MainWindow::syncCounters()
{
    for (int &c : m_counters)
        c = 0;
    static const QRegularExpression re("_(\\d+)$");
    for (const Element &e : m_elements) {
        const auto m = re.match(e.name);
        if (m.hasMatch())
            m_counters[int(e.kind)] = qMax(m_counters[int(e.kind)], m.captured(1).toInt());
    }
}

bool MainWindow::saveProject()
{
    QString path = m_filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save project"),
                                            QStringLiteral("layout.pixui.json"),
                                            "Pixel UI (*.pixui.json)");
        if (path.isEmpty())
            return false;
        path = withForcedSuffix(path, QStringLiteral(".pixui.json"));
    }

    QJsonObject root;
    QJsonObject scr;
    scr["width"] = m_screenW;
    scr["height"] = m_screenH;
    scr["background"] = m_bgColor.name(QColor::HexArgb);
    root["screen"] = scr;
    if (m_qmlMode) {
        root["mode"] = QStringLiteral("qml");
        root["qmlDir"] = m_qmlPane->projectDir();
        root["qmlEntry"] = m_qmlPane->entryFile();
        root["qmlVars"] = QJsonObject::fromVariantMap(m_qmlPane->vars());
    } else {
        root["mode"] = QStringLiteral("layout");
        QJsonArray arr;
        for (const Element &e : m_elements)
            arr.append(e.toJson());
        root["elements"] = arr;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("QPixelDesigner"),
                             tr("Cannot write file %1").arg(path));
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    f.close();

    m_filePath = path;
    m_dirty = false;
    refreshTitle();
    return true;
}

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), QString(), "Pixel UI (*.pixui.json *.json)");
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("QPixelDesigner"),
                             tr("Cannot open file %1").arg(path));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("QPixelDesigner"),
                             tr("File %1 does not look like a Pixel UI project").arg(path));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject scr = root["screen"].toObject();
    m_screenW = qBound(8, scr["width"].toInt(128), 2048);
    m_screenH = qBound(8, scr["height"].toInt(160), 2048);
    m_bgColor = QColor(scr["background"].toString("#ffffffff"));
    if (!m_bgColor.isValid())
        m_bgColor = Qt::white;

    const bool isQml = root["mode"].toString() == QLatin1String("qml");

    m_elements.clear();
    if (!isQml) {
        const QJsonArray arr = root["elements"].toArray();
        for (const QJsonValue &v : arr)
            m_elements.push_back(Element::fromJson(v.toObject()));
    }

    syncCounters();
    m_canvas->setBackgroundColor(m_bgColor);
    applyScreenSize(m_screenW, m_screenH);
    m_canvas->clearSelection();
    rebuildList();
    refreshProps();
    setColorButton(m_bgBtn, m_bgColor);

    if (isQml) {
        if (!m_qmlModeAct->isChecked())
            m_qmlModeAct->setChecked(true);
        const QString dir = root["qmlDir"].toString();
        const QString entry = root["qmlEntry"].toString();
        if (!dir.isEmpty() && QDir(dir).exists()) {
            m_qmlPane->openProjectDir(dir, entry);
        } else {
            if (!dir.isEmpty())
                QMessageBox::warning(this, tr("QPixelDesigner"),
                                     tr("QML folder %1 not found, loading the demo project").arg(dir));
            m_qmlPane->loadDemoProject();
        }
        if (root.contains(QLatin1String("qmlVars")))
            m_qmlPane->setVars(root["qmlVars"].toObject().toVariantMap());
        const QString legacy = root["qml"].toString();
        if (!legacy.isEmpty())
            m_qmlPane->writeEntrySource(legacy);
    } else if (m_qmlModeAct->isChecked()) {
        m_qmlModeAct->setChecked(false);
    }

    m_filePath = path;
    m_dirty = false;
    refreshTitle();
    refreshView();
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_elements.clear();
    for (int &c : m_counters)
        c = 0;
    m_filePath.clear();
    m_bgColor = Qt::white;
    m_canvas->setBackgroundColor(m_bgColor);
    setColorButton(m_bgBtn, m_bgColor);
    applyScreenSize(128, 160);
    m_canvas->clearSelection();
    rebuildList();
    refreshProps();
    m_dirty = false;
    refreshTitle();
    if (m_qmlMode)
        m_qmlPane->loadDemoProject();
    else
        refreshView();
}

void MainWindow::exportPng()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export PNG"),
                                                QStringLiteral("screen.png"), "PNG (*.png)");
    if (path.isEmpty())
        return;
    path = withForcedSuffix(path, QStringLiteral(".png"));
    if (m_lastRender.size() != QSize(m_screenW, m_screenH))
        m_lastRender = renderScreen(m_elements, m_screenW, m_screenH, m_bgColor);
    if (!m_lastRender.save(path))
        QMessageBox::warning(this, tr("QPixelDesigner"),
                             tr("Cannot save %1").arg(path));
}

void MainWindow::exportCArray()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("RGB565 C array"));
    auto *grid = new QGridLayout(&dlg);

    grid->addWidget(new QLabel(tr("Array name:")), 0, 0);
    auto *nameEdit = new QLineEdit(QStringLiteral("image_data"), &dlg);
    static const QRegularExpression idRe("^[A-Za-z_][A-Za-z0-9_]*$");
    nameEdit->setValidator(new QRegularExpressionValidator(idRe, &dlg));
    grid->addWidget(nameEdit, 0, 1, 1, 3);

    grid->addWidget(new QLabel(tr("Contrast:")), 1, 0);
    auto *contrast = new QDoubleSpinBox(&dlg);
    contrast->setRange(0.1, 5.0);
    contrast->setSingleStep(0.1);
    contrast->setValue(1.0);
    grid->addWidget(contrast, 1, 1);
    grid->addWidget(new QLabel(tr("Saturation:")), 1, 2);
    auto *saturation = new QDoubleSpinBox(&dlg);
    saturation->setRange(0.0, 5.0);
    saturation->setSingleStep(0.1);
    saturation->setValue(1.0);
    grid->addWidget(saturation, 1, 3);

    grid->addWidget(new QLabel(tr("Brightness:")), 2, 0);
    auto *brightness = new QSpinBox(&dlg);
    brightness->setRange(-255, 255);
    brightness->setToolTip(tr("ffmpeg eq uses brightness=-1..1 as a fraction of the full range"));
    grid->addWidget(brightness, 2, 1);
    grid->addWidget(new QLabel(tr("Byte order:")), 2, 2);
    auto *order = new QComboBox(&dlg);
    order->addItem(QStringLiteral("big-endian (rgb565be)"));
    order->addItem(QStringLiteral("little-endian (rgb565le)"));
    grid->addWidget(order, 2, 3);

    auto *info = new QLabel(&dlg);
    grid->addWidget(info, 3, 0, 1, 4);
    auto updateInfo = [=] {
        info->setText(tr("%1x%2 px -> %3 bytes")
                          .arg(m_screenW).arg(m_screenH)
                          .arg(m_screenW * m_screenH * 2));
    };
    connect(contrast, &QDoubleSpinBox::valueChanged, updateInfo);
    connect(saturation, &QDoubleSpinBox::valueChanged, updateInfo);
    connect(brightness, &QSpinBox::valueChanged, updateInfo);
    connect(order, &QComboBox::currentIndexChanged, updateInfo);
    updateInfo();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Generate…"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    grid->addWidget(bb, 4, 0, 1, 4);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString path = withForcedSuffix(
        QFileDialog::getSaveFileName(this, tr("Save header"),
                                     nameEdit->text() + QStringLiteral(".h"),
                                     QStringLiteral("C header (*.h)")),
        QStringLiteral(".h"));
    if (path.isEmpty())
        return;

    QImage img = m_lastRender;
    if (img.size() != QSize(m_screenW, m_screenH))
        img = renderScreen(m_elements, m_screenW, m_screenH, m_bgColor);
    img = applyEqFilter(img, contrast->value(), saturation->value(), brightness->value());
    const QByteArray bytes = toRgb565(img, order->currentIndex() == 0);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("QPixelDesigner"),
                             tr("Cannot write file %1").arg(path));
        return;
    }
    f.write(toXxdHeader(bytes, nameEdit->text(), m_screenW, m_screenH,
                        order->currentIndex() == 0).toUtf8());
    f.close();

    statusBar()->showMessage(tr("Saved: %1 (%2 bytes)").arg(path).arg(bytes.size()), 5000);
}


bool MainWindow::maybeSave()
{
    if (!m_dirty)
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("QPixelDesigner"), tr("The project has been modified. Save changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return saveProject();
    return ret != QMessageBox::Cancel;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}
