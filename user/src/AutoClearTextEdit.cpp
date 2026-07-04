/**
 * @file AutoClearTextEdit.cpp
 * 失去焦点时可清空的文本框控件。
 */
#include "AutoClearTextEdit.h"

// 构造函数
AutoClearTextEdit::AutoClearTextEdit(QWidget *parent)
    : QTextEdit(parent)
{
}

// 带文本的构造函数
AutoClearTextEdit::AutoClearTextEdit(const QString &text, QWidget *parent)
    : QTextEdit(parent)
{
    setPlainText(text);
}

// 失去焦点处理
void AutoClearTextEdit::focusOutEvent(QFocusEvent *event)
{
    QTextCursor cursor = this->textCursor();
    cursor.clearSelection();
    this->setTextCursor(cursor);
    QTextEdit::focusOutEvent(event);
}
