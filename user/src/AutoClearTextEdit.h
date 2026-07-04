#ifndef AUTOCLEARTEXTEDIT_H
#define AUTOCLEARTEXTEDIT_H

/**
 * @file AutoClearTextEdit.h
 * 失焦时可清空的文本框。
 */

#include <QObject>
#include <QTextEdit>
#include <QWidget>
#include <QFocusEvent>

// 自动清除选中状态文本框
class AutoClearTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    // 构造函数
    explicit AutoClearTextEdit(QWidget *parent = nullptr);
    // 带文本的构造函数
    explicit AutoClearTextEdit(const QString &text, QWidget *parent = nullptr);

protected:
    // 失去焦点处理
    void focusOutEvent(QFocusEvent *event) override;
};

#endif // 结束
