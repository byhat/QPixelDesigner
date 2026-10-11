#pragma once

#include <QPlainTextEdit>

/**
 * @brief Source editor with basic Qt Creator-like editing behavior.
 *
 * Keeps the indentation level on Enter (and indents after an opening
 * brace), auto-closes brackets and quotes, wraps the selection, deletes
 * matching pairs with Backspace, indents blocks with Tab/Shift+Tab and
 * toggles // comments with Ctrl+/.
 */
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool handleEnter();
    bool handleChar(const QString &ch);
    bool handleBackspace();
    bool handleTab(bool backward);
    bool handleCommentToggle();
    void indentLines(int first, int last, int delta);
    static QString matchingClose(const QString &open);
};
