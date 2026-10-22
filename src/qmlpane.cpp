#include "qmlpane.h"

#include "codeeditor.h"
#include "qmlhighlighter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGroupBox>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQmlError>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QXmlStreamReader>

/** @brief Paints scene frames grabbed from a hidden QQuickWindow
 *  (ApplicationWindow roots cannot live inside a QQuickWidget). */
class QmlFrameView : public QWidget
{
public:
    using QWidget::QWidget;

    void setFrame(const QImage &img)
    {
        m_pix = QPixmap::fromImage(img);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        if (m_pix.isNull())
            return;
        const double s = qMin(width() / double(m_pix.width()),
                              height() / double(m_pix.height()));
        const int w = qMax(1, int(m_pix.width() * s));
        const int h = qMax(1, int(m_pix.height() * s));
        p.drawPixmap(QRect((width() - w) / 2, (height() - h) / 2, w, h), m_pix);
    }

private:
    QPixmap m_pix;
};

/** @brief Threshold frame processing: Qt Quick antialiasing renders small
 *  text as gray pixels that do not exist on a pixel display. GrayToBw
 *  binarizes gray pixels only and keeps colored elements; Mono1Bit converts
 *  the whole frame to black and white. */
static QImage snapFrame(const QImage &src, FrameSnap mode)
{
    if (mode == FrameSnap::Off)
        return src;
    QImage out = src.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb c = line[x];
            if (mode == FrameSnap::Mono1Bit) {
                line[x] = qGray(c) < 128 ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
                continue;
            }
            const int lo = qMin(qRed(c), qMin(qGreen(c), qBlue(c)));
            const int hi = qMax(qRed(c), qMax(qGreen(c), qBlue(c)));
            if (hi - lo <= 40)
                line[x] = qGray(c) < 128 ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
        }
    }
    return out;
}

/**
 * @brief Maps qrc:/ URLs to files of the project folder.
 *
 * Emulates Qt resource compilation: .qrc files of the project are parsed
 * and "qrc:<prefix>/<alias>" URLs are rewritten to local file URLs, so the
 * same source code runs both here and in the compiled project.
 */
class QrcUrlInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    void setMap(const QHash<QString, QString> &map) { m_map = map; }

    QUrl intercept(const QUrl &url, DataType type) override
    {
        Q_UNUSED(type);
        if (url.scheme() != QLatin1String("qrc"))
            return url;
        const auto it = m_map.constFind(url.path());
        if (it != m_map.constEnd())
            return QUrl::fromLocalFile(it.value());
        return url;
    }

private:
    QHash<QString, QString> m_map; ///< "/prefix/file" -> absolute file path
};

static const char *kDemoMain = R"QML(// Coordinates are in screen pixels.
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 128
    height: 160
    color: "#ffffff"

    // Values from the C++ side (the "backend" map in the editor); the
    // fallbacks keep this file runnable in any plain Qt Quick project.
    readonly property string temp:
        typeof backend !== "undefined" && backend.temp !== undefined
            ? String(backend.temp) : "22.5"
    readonly property string mode:
        typeof backend !== "undefined" && backend.mode !== undefined
            ? String(backend.mode) : "Home"

    Text {
        x: 6; y: 6
        text: "THERMOSTAT"
        font.pixelSize: 11
        color: "#111111"
        renderType: Text.NativeRendering
    }

    Image {
        x: 104; y: 5
        width: 18; height: 18
        source: "qrc:/logo.png"
    }

    Rectangle { x: 6; y: 22; width: 116; height: 1; color: "#000000" }

    Text {
        x: 6; y: 86
        text: root.temp + " \u00B0C"
        font.pixelSize: 16
        renderType: Text.NativeRendering
    }

    MenuButton { x: 6; y: 30; label: "1. " + root.mode }
    MenuButton { x: 6; y: 54; label: "2. Settings" }

    Rectangle {
        x: 6; y: 130; width: 116; height: 20
        radius: 9
        color: "#146edc"

        Text {
            anchors.centerIn: parent
            text: "OK"
            color: "#ffffff"
            font.pixelSize: 10
            renderType: Text.NativeRendering
        }
    }
}
)QML";

static const char *kDemoMenuButton = R"QML(// A reusable component from a separate file.
import QtQuick

Rectangle {
    id: root

    property string label: ""

    width: 116
    height: 20
    radius: 4
    color: "transparent"
    border.color: "#000000"

    Text {
        x: 6; y: 5
        text: root.label
        font.pixelSize: 10
        renderType: Text.NativeRendering
    }
}
)QML";

static const char *kDemoQrc = R"XML(<RCC>
    <qresource prefix="/">
        <file>logo.png</file>
    </qresource>
</RCC>
)XML";

// ApplicationWindow ignores resizes after creation, so instances are
// created directly at the target size.
static QQuickWindow *createSceneWindow(QQmlEngine *engine, const QUrl &url, const QSize &size)
{
    QQmlComponent comp(engine, url);
    if (comp.isError())
        return nullptr;
    const QVariantMap init{{QStringLiteral("width"), size.width()},
                           {QStringLiteral("height"), size.height()}};
    QObject *root = comp.createWithInitialProperties(init);
    QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);
    return qobject_cast<QQuickWindow *>(root);
}

QmlPane::QmlPane(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

QmlPane::~QmlPane()
{
    delete m_interceptor; // not owned by the engine
}

void QmlPane::buildUi()
{
    setAcceptDrops(true);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *openBtn = new QPushButton(tr("Open folder…"));
    auto *newBtn = new QPushButton(tr("New file"));
    auto *reloadBtn = new QPushButton(tr("Reload (F5)"));
    auto *autoCheck = new QCheckBox(tr("Auto"));
    autoCheck->setChecked(true);
    autoCheck->setToolTip(tr("Auto-save and reload the scene while typing"));
    m_snapCombo = new QComboBox;
    m_snapCombo->addItem(tr("Gray -> B/W (LCD look)"));
    m_snapCombo->addItem(tr("Full 1-bit (monochrome)"));
    m_snapCombo->addItem(tr("No processing"));
    m_snapCombo->setCurrentIndex(2);
    m_snapCombo->setToolTip(tr("Gray -> B/W binarizes gray pixels (1-bit feel) and "
                               "destroys image gradients; No processing shows the "
                               "frame exactly as the display would"));
    m_entryLabel = new QLabel;
    top->addWidget(openBtn);
    top->addWidget(newBtn);
    top->addWidget(reloadBtn);
    top->addWidget(autoCheck);
    top->addWidget(m_snapCombo);
    top->addStretch(1);
    top->addWidget(m_entryLabel);
    lay->addLayout(top);

    m_tree = new QTreeView;
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilters({"*.qml", "qmldir", "*.qrc", "*.png", "*.jpg"});
    m_fsModel->setNameFilterDisables(false);
    m_tree->setModel(m_fsModel);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    for (int c = 1; c < m_tree->header()->count(); ++c)
        m_tree->setColumnHidden(c, true);
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (m_fsModel->isDir(index))
            return;
        const QString rel = m_dir.isEmpty()
                                ? QString()
                                : QDir(m_dir).relativeFilePath(m_fsModel->filePath(index));
        if (!rel.isEmpty() && !rel.startsWith(".."))
            openFile(rel);
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_tree->indexAt(pos);
        if (!index.isValid() || m_fsModel->isDir(index) || m_dir.isEmpty())
            return;
        const QString rel = QDir(m_dir).relativeFilePath(m_fsModel->filePath(index));
        if (rel.isEmpty() || rel.startsWith(".."))
            return;
        QMenu menu;
        menu.addAction(tr("Set as root scene"), this, [this, rel] { setEntry(rel); });
        menu.exec(m_tree->mapToGlobal(pos));
    });

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &QmlPane::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_lastTabIndex >= 0 && m_lastTabIndex < m_tabs->count() && m_lastTabIndex != index)
            saveFile(m_lastTabIndex);
        m_lastTabIndex = index;
    });

    m_log = new QPlainTextEdit;
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setReadOnly(true);
    m_log->setFixedHeight(80);
    m_log->setPlaceholderText(tr("QML errors and warnings appear here"));

    auto *editorSplit = new QSplitter(Qt::Vertical);
    editorSplit->addWidget(m_tabs);
    editorSplit->addWidget(m_log);
    editorSplit->setStretchFactor(0, 1);

    auto *mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->addWidget(m_tree);
    mainSplit->addWidget(editorSplit);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setSizes({200, 600});
    lay->addWidget(mainSplit, 1);

    auto *varsBox = new QGroupBox(tr("Backend map (backend.* in QML)"));
    auto *varsLay = new QVBoxLayout(varsBox);
    m_varsTable = new QTableWidget(0, 2);
    m_varsTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_varsTable->horizontalHeader()->setStretchLastSection(true);
    m_varsTable->verticalHeader()->setVisible(false);
    m_varsTable->setMaximumHeight(90);
    connect(m_varsTable, &QTableWidget::itemChanged, this, &QmlPane::onVarEdited);
    varsLay->addWidget(m_varsTable);
    auto *varsRow = new QHBoxLayout;
    auto *addVar = new QPushButton(tr("+ Variable"));
    auto *delVar = new QPushButton(tr("- Remove"));
    connect(addVar, &QPushButton::clicked, this, [this] {
        m_fillingVars = true;
        const int r = m_varsTable->rowCount();
        m_varsTable->insertRow(r);
        QString key = QStringLiteral("key1");
        for (int i = 1; m_vars->keys().contains(key); ++i)
            key = QStringLiteral("key%1").arg(i + 1);
        m_varsTable->setItem(r, 0, new QTableWidgetItem(key));
        m_varsTable->setItem(r, 1, new QTableWidgetItem(QStringLiteral("0")));
        m_fillingVars = false;
        applyVarsFromTable();
    });
    connect(delVar, &QPushButton::clicked, this, [this] {
        const int r = m_varsTable->currentRow();
            if (r < 0)
            return;
        m_fillingVars = true;
        m_varsTable->removeRow(r);
        m_fillingVars = false;
        applyVarsFromTable();
    });
    varsRow->addWidget(addVar);
    varsRow->addWidget(delVar);
    varsRow->addStretch(1);
    varsLay->addLayout(varsRow);
    lay->addWidget(varsBox);

    m_engine = new QQmlEngine(this);
    m_vars = QQmlPropertyMap::create(this); // plain ctor lacks QML integration
    m_engine->rootContext()->setContextProperty(QStringLiteral("backend"), m_vars);
    m_interceptor = new QrcUrlInterceptor;
    m_engine->addUrlInterceptor(m_interceptor);
    connect(m_engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
        for (const auto &w : warnings)
            appendLog(w.toString());
    });

    // hidden renderer: frames go to the central QML canvas via frameUpdated()
    m_quick = new QQuickWidget(m_engine, this);
    m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quick->setClearColor(Qt::white);
    m_quick->setFixedSize(m_w, m_h);
    m_quick->hide();
    connect(m_quick, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status st) {
        if (st == QQuickWidget::Error)
            for (const auto &err : m_quick->errors())
                appendLog(err.toString());
    });

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(600);
    connect(&m_debounce, &QTimer::timeout, this, &QmlPane::reloadNow);

    connect(&m_grabTimer, &QTimer::timeout, this, [this] {
        QImage img;
        if (m_hiddenWindow) {
            img = m_hiddenWindow->grabWindow();
        } else if (m_quick && m_quick->status() == QQuickWidget::Ready) {
            img = m_quick->grabFramebuffer();
        }
        if (img.isNull())
            return;
        if (m_previewWindow && m_previewView)
            m_previewView->setFrame(m_previewWindow->grabWindow());
        if (img.size() != QSize(m_w, m_h))
            img = img.scaled(m_w, m_h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        img = snapFrame(img, FrameSnap(m_snapCombo->currentIndex()));
        if (img != m_lastFrame) {
            m_lastFrame = img;
            emit frameUpdated(img);
        }
    });
    m_grabTimer.setInterval(60);

    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open QML project folder"));
        if (!dir.isEmpty())
            openProjectDir(dir);
    });
    connect(newBtn, &QPushButton::clicked, this, &QmlPane::newFile);
    connect(reloadBtn, &QPushButton::clicked, this, &QmlPane::reloadNow);
    connect(autoCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!on)
            m_debounce.stop();
        else if (m_tabs->currentWidget())
            m_debounce.start();
    });

    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, &QmlPane::reloadNow);
}

QWidget *QmlPane::previewWidget()
{
    if (!m_previewHost) {
        m_previewHost = new QWidget(nullptr);
        auto *lay = new QVBoxLayout(m_previewHost);
        lay->setContentsMargins(0, 0, 0, 0);
        if (!m_dir.isEmpty() && !m_entry.isEmpty())
            rebuildPreview(QUrl::fromLocalFile(QDir(m_dir).filePath(m_entry)));
    }
    return m_previewHost;
}

// Видимое превью поддерживает оба вида корня: Item рендерится в QQuickWidget,
// ApplicationWindow (QQuickWindow) встраивается через createWindowContainer.
void QmlPane::rebuildPreview(const QUrl &url)
{
    if (!m_previewHost)
        return;
    auto *lay = static_cast<QVBoxLayout *>(m_previewHost->layout());
    while (lay->count() > 0)
        delete lay->takeAt(0)->widget();
    delete m_previewWindow;
    m_previewWindow = nullptr;
    m_previewView = nullptr;

    QQmlComponent probe(m_engine, url);
    if (probe.isError()) {
        for (const auto &e : probe.errors())
            appendLog(e.toString());
        return;
    }
    QObject *root = probe.create();
    QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);
    if (qobject_cast<QQuickWindow *>(root)) {
        delete root;
        // ApplicationWindow ignores resizes after creation, so the preview
        // renders at a fixed 2x screen size and the view scales the frame
        m_previewWindow = createSceneWindow(m_engine, url, QSize(m_w * 2, m_h * 2));
        m_previewView = new QmlFrameView;
        lay->addWidget(m_previewView);
        return;
    }
    delete root;

    auto *qw = new QQuickWidget(m_engine, nullptr);
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setClearColor(Qt::white);
    connect(qw, &QQuickWidget::statusChanged, this, [this, qw](QQuickWidget::Status st) {
        if (st == QQuickWidget::Error)
            for (const auto &err : qw->errors())
                appendLog(err.toString());
    });
    qw->setSource(url);
    lay->addWidget(qw);
}

void QmlPane::setActive(bool active)
{
    if (active)
        m_grabTimer.start();
    else
        m_grabTimer.stop();
}

void QmlPane::dragEnterEvent(QDragEnterEvent *event)
{
    const auto urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        const QFileInfo fi(u.toLocalFile());
        if (fi.isDir() || (fi.isFile() && fi.suffix() == "qml")) {
            event->acceptProposedAction();
            return;
        }
    }
}

void QmlPane::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QFileInfo fi(urls.first().toLocalFile());
    if (fi.isDir()) {
        openProjectDir(fi.absoluteFilePath());
    } else if (fi.suffix() == "qml") {
        openProjectDir(fi.absolutePath(), fi.fileName() == "main.qml" ? fi.fileName()
                                                                     : QString());
    }
    event->acceptProposedAction();
}

void QmlPane::setScreenSize(int width, int height)
{
    if (width == m_w && height == m_h)
        return;
    m_w = width;
    m_h = height;
    if (m_quick)
        m_quick->setFixedSize(m_w, m_h);
    // the hidden window is created at a fixed size, so a screen size change
    // requires a reload to re-create the scene
    if (!m_dir.isEmpty())
        QTimer::singleShot(0, this, &QmlPane::reloadNow);
}

bool QmlPane::openProjectDir(const QString &dir, const QString &entry)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return false;

    m_dir = dir;
    while (m_tabs->count() > 0)
        closeTab(0);
    m_tree->setRootIndex(m_fsModel->setRootPath(dir));
    pickDefaultEntry();
    if (!entry.isEmpty() && QFileInfo::exists(QDir(dir).filePath(entry)))
        m_entry = entry;

    openFile(m_entry);
    updateEntryLabel();
    reloadNow();
    return true;
}

void QmlPane::loadDemoProject()
{
    m_tempDir = QTemporaryDir();
    if (!m_tempDir.isValid())
        return;

    QImage logo(24, 24, QImage::Format_ARGB32);
    logo.fill(Qt::transparent);
    QPainter p(&logo);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#146edc"));
    p.drawRect(2, 2, 20, 20);
    p.setBrush(Qt::white);
    p.drawRect(7, 7, 10, 10);
    p.end();
    logo.save(m_tempDir.filePath(QStringLiteral("logo.png")));

    const QList<QPair<QString, QByteArray>> files = {
        {QStringLiteral("main.qml"), QByteArray(kDemoMain)},
        {QStringLiteral("MenuButton.qml"), QByteArray(kDemoMenuButton)},
        {QStringLiteral("demo.qrc"), QByteArray(kDemoQrc)},
    };
    for (const auto &f : files) {
        QFile out(m_tempDir.filePath(f.first));
        if (!out.open(QIODevice::WriteOnly))
            return;
        out.write(f.second);
    }
    openProjectDir(m_tempDir.path(), QStringLiteral("main.qml"));
    setVars({{QStringLiteral("temp"), 22.5},
             {QStringLiteral("mode"), QStringLiteral("Home")}});
}

bool QmlPane::writeEntrySource(const QString &code)
{
    if (m_dir.isEmpty() || m_entry.isEmpty())
        return false;
    QFile f(QDir(m_dir).filePath(m_entry));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(code.toUtf8());
    f.close();
    openFile(m_entry);
    reloadNow();
    return true;
}

QString QmlPane::logText() const
{
    return m_log ? m_log->toPlainText() : QString();
}

void QmlPane::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void QmlPane::scanQrc()
{
    QHash<QString, QString> map;
    QDirIterator it(m_dir, {"*.qrc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString qrcPath = it.next();
        QFile f(qrcPath);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QXmlStreamReader xml(&f);
        QString prefix = "/";
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() == QLatin1String("qresource")) {
                prefix = xml.attributes().value("prefix").toString();
                if (prefix.isEmpty())
                    prefix = "/";
                if (!prefix.startsWith('/'))
                    prefix.prepend('/');
                while (prefix.size() > 1 && prefix.endsWith('/'))
                    prefix.chop(1);
            } else if (xml.name() == QLatin1String("file")) {
                const QString alias = xml.attributes().value("alias").toString();
                const QString file = xml.readElementText().trimmed();
                if (file.isEmpty())
                    continue;
                // without an alias the resource keeps the listed path:
                // <file>img/logo.png</file> -> :/img/logo.png
                const QString name = alias.isEmpty() ? QDir::cleanPath(file) : alias;
                const QString target =
                    QFileInfo(QDir(QFileInfo(qrcPath).absolutePath()), file).absoluteFilePath();
                map.insert((prefix == "/" ? QString() : prefix) + "/" + name, target);
            }
        }
    }
    m_interceptor->setMap(map);
}

void QmlPane::openFile(const QString &relPath)
{
    if (m_dir.isEmpty() || relPath.isEmpty())
        return;

    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property("relPath").toString() == relPath) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    QFile f(QDir(m_dir).filePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QString code = QString::fromUtf8(f.readAll());
    f.close();

    auto *editor = new CodeEditor;
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setTabStopDistance(4 * editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    new QmlHighlighter(editor->document());
    editor->blockSignals(true);
    editor->setPlainText(code);
    editor->blockSignals(false);
    connect(editor, &QPlainTextEdit::textChanged, this, &QmlPane::onTextChanged);

    const QString name = QFileInfo(relPath).fileName();
    const int idx = m_tabs->addTab(editor, name);
    m_tabs->setTabToolTip(idx, relPath);
    m_tabs->widget(idx)->setProperty("relPath", relPath);
    m_tabs->setCurrentIndex(idx);
    editor->document()->setModified(false);
}

void QmlPane::onTextChanged()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0)
        return;
    QWidget *w = m_tabs->widget(idx);
    const QString relPath = w->property("relPath").toString();
    if (!relPath.isEmpty() && !m_tabs->tabText(idx).startsWith("* "))
        m_tabs->setTabText(idx, "* " + QFileInfo(relPath).fileName());
    m_debounce.start();
    emit sourceEdited();
}

bool QmlPane::saveFile(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= m_tabs->count())
        return false;
    QWidget *w = m_tabs->widget(tabIndex);
    const QString relPath = w->property("relPath").toString();
    auto *editor = qobject_cast<QPlainTextEdit *>(w);
    if (!editor || relPath.isEmpty())
        return false;

    if (!editor->document()->isModified()) {
        m_tabs->setTabText(tabIndex, QFileInfo(relPath).fileName());
        return true;
    }

    QFile f(QDir(m_dir).filePath(relPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(editor->toPlainText().toUtf8());
    f.close();
    editor->document()->setModified(false);
    m_tabs->setTabText(tabIndex, QFileInfo(relPath).fileName());
    return true;
}

void QmlPane::closeTab(int index)
{
    saveFile(index);
    QWidget *w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete w;
    if (m_lastTabIndex >= m_tabs->count())
        m_lastTabIndex = m_tabs->count() - 1;
}

void QmlPane::setEntry(const QString &relPath)
{
    m_entry = relPath;
    updateEntryLabel();
    reloadNow();
}

void QmlPane::pickDefaultEntry()
{
    const QDir dir(m_dir);
    const QStringList files = dir.entryList({"*.qml"}, QDir::Files, QDir::Name);
    if (files.contains("main.qml"))
        m_entry = "main.qml";
    else if (!files.isEmpty())
        m_entry = files.first();
    else
        m_entry.clear();
}

void QmlPane::updateEntryLabel()
{
    m_entryLabel->setText(tr("Root scene: %1").arg(m_entry.isEmpty() ? "—" : m_entry));
}

void QmlPane::newFile()
{
    if (m_dir.isEmpty())
        return;
    static const QRegularExpression re("^[A-Za-z0-9_][A-Za-z0-9_\\-]*\\.qml$");
    const QString name = QInputDialog::getText(
        this, tr("New QML file"), tr("File name:"),
        QLineEdit::Normal, QStringLiteral("Component.qml"));
    if (name.isEmpty() || !re.match(name).hasMatch())
        return;

    QFile f(QDir(m_dir).filePath(name));
    if (f.exists()) {
        openFile(name);
        return;
    }
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write("import QtQuick\n\nRectangle {\n}\n");
    f.close();
    openFile(name);
}

QVariantMap QmlPane::vars() const
{
    return m_varsContent;
}

void QmlPane::setVars(const QVariantMap &vars)
{
    if (!m_varsTable)
        return;
    m_fillingVars = true;
    m_varsTable->setRowCount(int(vars.size()));
    int r = 0;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it, ++r) {
        m_varsTable->setItem(r, 0, new QTableWidgetItem(it.key()));
        m_varsTable->setItem(r, 1, new QTableWidgetItem(it.value().toString()));
    }
    m_fillingVars = false;
    applyVars(vars);
}

void QmlPane::applyVars(const QVariantMap &vars)
{
    // QQmlPropertyMap::clear() keeps the key with an empty value, and the
    // engine re-materializes properties the scene has read, so the QML
    // object cannot serve as the source of truth. The map is rebuilt from
    // scratch on every change and m_varsContent keeps the exact contents.
    m_varsContent = vars;
    auto *fresh = QQmlPropertyMap::create(this);
    for (auto it = vars.cbegin(); it != vars.cend(); ++it)
        fresh->insert(it.key(), it.value());
    m_engine->rootContext()->setContextProperty(QStringLiteral("backend"), fresh);
    QQmlPropertyMap *old = m_vars;
    m_vars = fresh;
    if (old)
        old->deleteLater();
}

void QmlPane::applyVarsFromTable()
{
    QVariantMap vars;
    for (int r = 0; r < m_varsTable->rowCount(); ++r) {
        const QTableWidgetItem *keyItem = m_varsTable->item(r, 0);
        const QTableWidgetItem *valItem = m_varsTable->item(r, 1);
        if (!keyItem)
            continue;
        const QString key = keyItem->text().trimmed();
        if (key.isEmpty())
            continue;
        const QString val = valItem ? valItem->text().trimmed() : QString();
        bool isNumber = false;
        const double d = val.toDouble(&isNumber);
        vars.insert(key, (isNumber && !val.isEmpty()) ? QVariant(d) : QVariant(val));
    }
    applyVars(vars);
    emit varsEdited();
}

void QmlPane::onVarEdited(QTableWidgetItem *item)
{
    Q_UNUSED(item);
    if (m_fillingVars)
        return;
    applyVarsFromTable();
}

void QmlPane::reloadNow()
{
    if (m_dir.isEmpty() || m_entry.isEmpty())
        return;

    saveFile(m_tabs->currentIndex());

    m_log->clear();
    scanQrc();
    m_engine->clearComponentCache();
    const QUrl url = QUrl::fromLocalFile(QDir(m_dir).filePath(m_entry));

    // скрытый рендерер 1:1: ApplicationWindow -> собственное скрытое окно,
    // Item -> скрытый QQuickWidget
    delete m_hiddenRoot;
    m_hiddenWindow = nullptr;
    m_quick->setSource(QUrl());
    QQmlComponent probe(m_engine, url);
    if (probe.isError()) {
        for (const auto &e : probe.errors())
            appendLog(e.toString());
    } else if (QObject *root = probe.create()) {
        if (qobject_cast<QQuickWindow *>(root)) {
            delete root;
            m_hiddenWindow = createSceneWindow(m_engine, url, QSize(m_w, m_h));
            m_hiddenRoot = m_hiddenWindow;
        } else {
            delete root;
            m_quick->setSource(url);
            if (m_quick->status() == QQuickWidget::Error) {
                for (const auto &err : m_quick->errors())
                    appendLog(err.toString());
            }
        }
    }

    rebuildPreview(url);
}
