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
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QQmlEngine>
#include <QQmlError>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>

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

Rectangle {
    color: "#ffffff"

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

    MenuButton { x: 6; y: 30; label: "1. Home" }
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
    m_snapCombo->setToolTip(tr("QML antialiasing renders small text in gray; "
                               "the threshold turns gray pixels black/white and keeps colors"));
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

    m_engine = new QQmlEngine(this);
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
        if (!m_quick || m_quick->status() != QQuickWidget::Ready)
            return;
        QImage img = m_quick->grabFramebuffer();
        if (img.isNull())
            return;
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

QQuickWidget *QmlPane::previewWidget()
{
    if (!m_previewQuick) {
        m_previewQuick = new QQuickWidget(m_engine, nullptr);
        m_previewQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
        m_previewQuick->setClearColor(Qt::white);
        connect(m_previewQuick, &QQuickWidget::statusChanged, this,
                [this](QQuickWidget::Status st) {
                    if (st == QQuickWidget::Error)
                        for (const auto &err : m_previewQuick->errors())
                            appendLog(err.toString());
                });
        if (!m_dir.isEmpty() && !m_entry.isEmpty())
            m_previewQuick->setSource(QUrl::fromLocalFile(QDir(m_dir).filePath(m_entry)));
    }
    return m_previewQuick;
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
    m_w = width;
    m_h = height;
    if (m_quick)
        m_quick->setFixedSize(m_w, m_h);
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

void QmlPane::reloadNow()
{
    if (m_dir.isEmpty() || m_entry.isEmpty())
        return;

    saveFile(m_tabs->currentIndex());

    m_log->clear();
    scanQrc();
    m_engine->clearComponentCache();
    const QUrl url = QUrl::fromLocalFile(QDir(m_dir).filePath(m_entry));
    m_quick->setSource(QUrl());
    m_quick->setSource(url);
    if (m_quick->status() == QQuickWidget::Error) {
        for (const auto &err : m_quick->errors())
            appendLog(err.toString());
    }
    if (m_previewQuick) {
        m_previewQuick->setSource(QUrl());
        m_previewQuick->setSource(url);
    }
}
