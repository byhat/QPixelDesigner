#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

/** @brief QML syntax highlighting for the code editor. */
class QmlHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit QmlHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    /** @brief Highlighting rule: a regular expression and its format. */
    struct Rule {
        QRegularExpression re;
        QTextCharFormat fmt;
    };

    QVector<Rule> m_rules;
    QTextCharFormat m_blockCommentFmt;
    QRegularExpression m_commentStart{QStringLiteral("/\\*")};
    QRegularExpression m_commentEnd{QStringLiteral("\\*/")};
};
