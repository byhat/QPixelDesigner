#include "codeeditor.h"

#include <QKeyEvent>
#include <QTextBlock>

namespace {

constexpr int kIndent = 4;

struct Pair {
    const char *open;
    const char *close;
};

const Pair kPairs[] = {{"{", "}"}, {"(", ")"}, {"[", "]"}, {"\"", "\""}, {"'", "'"}};

bool isClosing(const QChar &c)
{
    return c == QLatin1Char('}') || c == QLatin1Char(')') || c == QLatin1Char(']')
           || c == QLatin1Char('"');
}

} // namespace

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
}

QString CodeEditor::matchingClose(const QString &open)
{
    for (const Pair &p : kPairs)
        if (open == QLatin1String(p.open))
            return QLatin1String(p.close);
    return QString();
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (handleEnter())
                return;
        } else if (event->key() == Qt::Key_Backspace) {
            if (handleBackspace())
                return;
        } else if (event->key() == Qt::Key_Tab) {
            if (handleTab(false))
                return;
        } else if (event->key() == Qt::Key_Backtab) {
            if (handleTab(true))
                return;
        } else {
            const QString text = event->text();
            if (!text.isEmpty() && handleChar(text))
                return;
        }
    }
    if (mods == Qt::ControlModifier && event->key() == Qt::Key_Slash) {
        if (handleCommentToggle())
            return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

bool CodeEditor::handleEnter()
{
    QTextCursor tc = textCursor();
    tc.beginEditBlock();
    if (tc.hasSelection())
        tc.removeSelectedText();

    const QString beforeCursor = tc.block().text().left(tc.columnNumber());
    QString indent;
    for (const QChar &c : beforeCursor) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
            indent += c;
        else
            break;
    }

    const bool opensBlock = beforeCursor.trimmed().endsWith(QLatin1Char('{'));
    if (opensBlock)
        indent += QString(kIndent, QLatin1Char(' '));

    // { + Enter with a closing brace right after: "    code" between the two
    const bool closesNext =
        opensBlock && document()->characterAt(tc.position()) == QLatin1Char('}');

    if (closesNext) {
        tc.insertText(QLatin1Char('\n') + indent);
        const int mid = tc.position();
        tc.insertText(QLatin1Char('\n') + indent.left(indent.size() - kIndent));
        tc.endEditBlock();
        tc.setPosition(mid);
    } else {
        tc.insertText(QLatin1Char('\n') + indent);
        tc.endEditBlock();
    }
    setTextCursor(tc);
    return true;
}

bool CodeEditor::handleChar(const QString &ch)
{
    if (ch.size() != 1)
        return false;
    const QChar c = ch.at(0);
    QTextCursor tc = textCursor();

    // '}' on a whitespace-only line start: dedent, then insert as usual
    if (c == QLatin1Char('}') && !tc.hasSelection()) {
        if (document()->characterAt(tc.position()) == c) {
            tc.movePosition(QTextCursor::Right);
            setTextCursor(tc);
            return true;
        }
        const QString before = tc.block().text().left(tc.columnNumber());
        if (before.trimmed().isEmpty()) {
            int n = qMin(kIndent, before.size());
            while (n > 0 && before.at(n - 1) != QLatin1Char(' '))
                --n;
            if (n > 0) {
                QTextCursor d = tc;
                d.beginEditBlock();
                d.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, n);
                d.removeSelectedText();
                d.endEditBlock();
                setTextCursor(d);
            }
        }
        return false;
    }

    const QString close = matchingClose(ch);
    if (close.isEmpty())
        return false;

    // typing a closing char that is already next: step over it
    if (ch == close && !tc.hasSelection()
        && document()->characterAt(tc.position()) == c) {
        tc.movePosition(QTextCursor::Right);
        setTextCursor(tc);
        return true;
    }

    if (tc.hasSelection()) {
        // wrap the selection with the pair
        const QString sel = tc.selectedText();
        tc.beginEditBlock();
        tc.insertText(ch + sel + close);
        tc.endEditBlock();
        const int end = tc.position();
        tc.setPosition(end - close.size() - sel.size());
        tc.setPosition(end - close.size(), QTextCursor::KeepAnchor);
        setTextCursor(tc);
        return true;
    }

    const QChar nxt = document()->characterAt(tc.position());
    if (nxt.isLetterOrNumber() || nxt == QLatin1Char('_') || isClosing(nxt))
        return false;

    tc.beginEditBlock();
    tc.insertText(ch + close);
    tc.endEditBlock();
    tc.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, close.size());
    setTextCursor(tc);
    return true;
}

bool CodeEditor::handleBackspace()
{
    QTextCursor tc = textCursor();
    if (tc.hasSelection())
        return false;
    const int pos = tc.position();
    if (pos == 0)
        return false;

    const QChar prev = document()->characterAt(pos - 1);
    const QChar next = document()->characterAt(pos);
    const bool pair = (prev == QLatin1Char('{') && next == QLatin1Char('}'))
                      || (prev == QLatin1Char('(') && next == QLatin1Char(')'))
                      || (prev == QLatin1Char('[') && next == QLatin1Char(']'))
                      || (prev == QLatin1Char('"') && next == QLatin1Char('"'))
                      || (prev == QLatin1Char('\'') && next == QLatin1Char('\''));
    if (!pair)
        return false;

    tc.beginEditBlock();
    tc.deletePreviousChar();
    tc.deleteChar();
    tc.endEditBlock();
    setTextCursor(tc);
    return true;
}

bool CodeEditor::handleTab(bool backward)
{
    QTextCursor tc = textCursor();
    const int first = document()->findBlock(tc.selectionStart()).blockNumber();
    const int last = document()->findBlock(tc.selectionEnd()).blockNumber();

    if (!tc.hasSelection() && first == last) {
        if (backward) {
            const QString t = tc.block().text();
            int n = 0;
            while (n < kIndent && n < t.size() && t.at(n) == QLatin1Char(' '))
                ++n;
            if (n == 0)
                return true;
            const int col = tc.columnNumber();
            QTextCursor c = tc;
            c.beginEditBlock();
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, n);
            c.removeSelectedText();
            c.endEditBlock();
            c.setPosition(tc.block().position() + qMax(0, col - n));
            setTextCursor(c);
        } else {
            insertPlainText(QString(kIndent, QLatin1Char(' ')));
        }
        return true;
    }

    indentLines(first, last, backward ? -kIndent : kIndent);
    return true;
}

void CodeEditor::indentLines(int first, int last, int delta)
{
    QTextCursor tc = textCursor();
    tc.beginEditBlock();
    for (int b = first; b <= last; ++b) {
        const QTextBlock block = document()->findBlockByNumber(b);
        if (!block.isValid())
            continue;
        QTextCursor c(block);
        if (delta > 0) {
            c.insertText(QString(delta, QLatin1Char(' ')));
        } else {
            const QString t = block.text();
            int n = 0;
            while (n < -delta && n < t.size() && t.at(n) == QLatin1Char(' '))
                ++n;
            if (n > 0) {
                c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, n);
                c.removeSelectedText();
            }
        }
    }
    tc.endEditBlock();

    QTextCursor sel = QTextCursor(document()->findBlockByNumber(first));
    QTextCursor end = QTextCursor(document()->findBlockByNumber(last));
    end.movePosition(QTextCursor::EndOfBlock);
    sel.setPosition(end.position(), QTextCursor::KeepAnchor);
    setTextCursor(sel);
}

bool CodeEditor::handleCommentToggle()
{
    QTextCursor tc = textCursor();
    const int first = document()->findBlock(tc.selectionStart()).blockNumber();
    const int last = document()->findBlock(tc.selectionEnd()).blockNumber();

    bool allCommented = true;
    for (int b = first; b <= last; ++b) {
        const QString t = document()->findBlockByNumber(b).text();
        if (!t.trimmed().isEmpty() && !t.trimmed().startsWith("//"))
            allCommented = false;
    }

    QTextCursor edit = textCursor();
    edit.beginEditBlock();
    for (int b = first; b <= last; ++b) {
        const QTextBlock block = document()->findBlockByNumber(b);
        if (!block.isValid() || block.text().trimmed().isEmpty())
            continue;
        QTextCursor c(block);
        if (allCommented) {
            const QString t = block.text();
            const int at = t.indexOf("//");
            if (at >= 0) {
                c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, at);
                c.setPosition(c.position() + 2, QTextCursor::KeepAnchor);
                if (document()->characterAt(c.position()) == QLatin1Char(' '))
                    c.setPosition(c.position() + 1, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else {
            int at = 0;
            while (at < block.text().size()
                   && (block.text().at(at) == QLatin1Char(' ')
                       || block.text().at(at) == QLatin1Char('\t')))
                ++at;
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, at);
            c.insertText(QStringLiteral("// "));
        }
    }
    edit.endEditBlock();
    return true;
}
