#include "qmlpane.h"

#include "qmlhighlighter.h"
#include "screenelement.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QQmlError>
#include <QQmlEngine>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickWidget>
#include <QShortcut>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>


QString qmlDemoSource()
{
    return QStringLiteral(
        "// Координаты — в пикселях экрана.\n"
        "import QtQuick\n"
        "\n"
        "Rectangle {\n"
        "    color: \"#ffffff\"\n"
        "\n"
        "    Text {\n"
        "        x: 6; y: 6\n"
        "        text: \"ТЕРМОСТАТ\"\n"
        "        font.pixelSize: 11\n"
        "        color: \"#111111\"\n"
        "        renderType: Text.NativeRendering\n"
        "    }\n"
        "\n"
        "    Rectangle {\n"
        "        x: 6; y: 22; width: 116; height: 1\n"
        "        color: \"#000000\"\n"
        "    }\n"
        "\n"
        "    Rectangle {\n"
        "        x: 6; y: 30; width: 116; height: 20\n"
        "        radius: 4\n"
        "        color: \"transparent\"\n"
        "        border.color: \"#000000\"\n"
        "\n"
        "        Text {\n"
        "            x: 6; y: 5\n"
        "            text: \"1. Главная\"\n"
        "            font.pixelSize: 10\n"
        "            renderType: Text.NativeRendering\n"
        "        }\n"
        "    }\n"
        "\n"
        "    Rectangle {\n"
        "        x: 6; y: 54; width: 116; height: 20\n"
        "        radius: 4\n"
        "        color: \"transparent\"\n"
        "        border.color: \"#000000\"\n"
        "\n"
        "        Text {\n"
        "            x: 6; y: 5\n"
        "            text: \"2. Настройки\"\n"
        "            font.pixelSize: 10\n"
        "            renderType: Text.NativeRendering\n"
        "        }\n"
        "    }\n"
        "\n"
        "    Rectangle {\n"
        "        x: 6; y: 130; width: 116; height: 20\n"
        "        radius: 9\n"
        "        color: \"#146edc\"\n"
        "\n"
        "        Text {\n"
        "            anchors.centerIn: parent\n"
        "            text: \"OK\"\n"
        "            color: \"#ffffff\"\n"
        "            font.pixelSize: 10\n"
        "            renderType: Text.NativeRendering\n"
        "        }\n"
        "    }\n"
        "}\n");
}

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

QmlPane::QmlPane(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *reloadBtn = new QPushButton(tr("Обновить (F5)"));
    auto *autoCheck = new QCheckBox(tr("Авто"));
    autoCheck->setChecked(true);
    autoCheck->setToolTip(tr("Перегружать сцену при наборе кода"));
    auto *openBtn = new QPushButton(tr("Открыть…"));
    auto *saveBtn = new QPushButton(tr("Сохранить…"));
    m_snapCombo = new QComboBox;
    m_snapCombo->addItem(tr("Серый → ч/б (как на LCD)"));
    m_snapCombo->addItem(tr("Полный 1-bit (монохром)"));
    m_snapCombo->addItem(tr("Без обработки"));
    m_snapCombo->setToolTip(tr("Сглаживание QML делает текст серым; порог превращает "
                               "серые пиксели в чёрные/белые, цвета не трогает"));
    top->addWidget(reloadBtn);
    top->addWidget(autoCheck);
    top->addWidget(m_snapCombo);
    top->addStretch(1);
    top->addWidget(openBtn);
    top->addWidget(saveBtn);
    lay->addLayout(top);

    auto *split = new QSplitter(Qt::Vertical);
    m_editor = new QPlainTextEdit;
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    new QmlHighlighter(m_editor->document());
    m_editor->installEventFilter(this);

    m_log = new QPlainTextEdit;
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setReadOnly(true);
    m_log->setFixedHeight(80);
    m_log->setPlaceholderText(tr("Ошибки и предупреждения QML появятся здесь"));

    split->addWidget(m_editor);
    split->addWidget(m_log);
    split->setStretchFactor(0, 1);
    lay->addWidget(split, 1);

    auto *previewRow = new QHBoxLayout;
    m_engine = new QQmlEngine(this);
    connect(m_engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
        for (const auto &w : warnings)
            appendLog(w.toString());
    });

    m_quick = new QQuickWidget(m_engine, this);
    m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quick->setClearColor(Qt::white);
    m_quick->setFixedSize(m_w, m_h);
    connect(m_quick, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status st) {
        if (st == QQuickWidget::Error)
            for (const auto &err : m_quick->errors())
                appendLog(err.toString());
    });
    previewRow->addWidget(m_quick);
    previewRow->addStretch(1);
    lay->addLayout(previewRow);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(600);
    connect(&m_debounce, &QTimer::timeout, this, &QmlPane::reloadNow);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        emit sourceEdited();
        if (m_quick)
            m_debounce.start();
    });

    connect(&m_grabTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !m_quick)
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
    m_grabTimer.start(60);

    connect(reloadBtn, &QPushButton::clicked, this, &QmlPane::reloadNow);
    connect(autoCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            reloadNow();
    });
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Открыть QML"), QString(),
                                                          "QML (*.qml)");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        setSourceText(QString::fromUtf8(f.readAll()));
    });
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, tr("Сохранить QML"),
                                                    QStringLiteral("scene.qml"), "QML (*.qml)");
        if (path.isEmpty())
            return;
        path = withForcedSuffix(path, QStringLiteral(".qml"));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(m_editor->toPlainText().toUtf8());
    });

    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, &QmlPane::reloadNow);
}

bool QmlPane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab && ke->modifiers() == Qt::NoModifier) {
            m_editor->insertPlainText(QStringLiteral("    "));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QmlPane::setScreenSize(int width, int height)
{
    m_w = width;
    m_h = height;
    if (m_quick)
        m_quick->setFixedSize(m_w, m_h);
}

void QmlPane::setSourceText(const QString &code)
{
    m_loading = true;
    m_editor->setPlainText(code);
    m_loading = false;
    reloadNow();
}

QString QmlPane::source() const
{
    return m_editor ? m_editor->toPlainText() : QString();
}

QString QmlPane::logText() const
{
    return m_log ? m_log->toPlainText() : QString();
}

void QmlPane::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void QmlPane::reloadNow()
{
    if (!m_quick || !m_tempDir.isValid())
        return;

    const QString path = m_tempDir.filePath(QStringLiteral("scene.qml"));
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(m_editor->toPlainText().toUtf8());
    }

    m_log->clear();
    m_engine->clearComponentCache();
    m_quick->setSource(QUrl());
    m_quick->setSource(QUrl::fromLocalFile(path));
    if (m_quick->status() == QQuickWidget::Error) {
        for (const auto &err : m_quick->errors())
            appendLog(err.toString());
    }
}
