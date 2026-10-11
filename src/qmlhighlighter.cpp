#include "qmlhighlighter.h"

#include <QColor>

QmlHighlighter::QmlHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    QTextCharFormat fmt;

    fmt.setForeground(QColor(0x6a, 0x99, 0x55));
    fmt.setFontItalic(true);
    Rule lineComment{QRegularExpression(QStringLiteral("//[^\n]*")), fmt};
    m_rules.append(lineComment);
    m_blockCommentFmt = fmt;

    fmt.setFontItalic(false);
    fmt.setForeground(QColor(0xa3, 0x15, 0x15));
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), fmt});

    fmt.setForeground(QColor(0x00, 0x00, 0xff));
    fmt.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression(QStringLiteral(
                         "\\b(?:import|as|property|readonly|id|signal|function|"
                         "component|if|else|for|while|return|true|false|null|"
                         "undefined|break|continue|const|let|var|new|typeof)\\b")),
                    fmt});

    fmt.setForeground(QColor(0x26, 0x7f, 0x99));
    fmt.setFontWeight(QFont::Normal);
    m_rules.append({QRegularExpression(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b")), fmt});

    fmt.setForeground(QColor(0x09, 0x86, 0x58));
    m_rules.append({QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")), fmt});
}

void QmlHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &r : m_rules) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), r.fmt);
        }
    }

    setCurrentBlockState(0);
    int start = 0;
    if (previousBlockState() != 1)
        start = text.indexOf(m_commentStart);
    while (start >= 0) {
        const auto endMatch = m_commentEnd.match(text, start);
        int len;
        if (!endMatch.hasMatch()) {
            setCurrentBlockState(1);
            len = text.length() - start;
        } else {
            len = endMatch.capturedStart() - start + endMatch.capturedLength();
        }
        setFormat(start, len, m_blockCommentFmt);
        start = text.indexOf(m_commentStart, start + len);
    }
}
