#ifndef CHOICEDIALOG_H
#define CHOICEDIALOG_H

/**
 * @file ChoiceDialog.h
 * 双按钮确认（接受/拒绝）。
 */

#include <QDialog>
#include <QString>
#include <QPainter>

namespace Ui {
class ChoiceDialog;
}

class ChoiceDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit ChoiceDialog(QWidget *parent = nullptr);
    // 析构函数
    ~ChoiceDialog();
    // 设置文本
    void transText(const QString &text);
    // 设置按钮文本
    void transButText(const QString& no, const QString& yes);

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);

private:
    // 初始化文本框
    void setEdit();

private slots:
    // 点击确认
    void on_but_yes_clicked();
    // 点击取消
    void on_but_no_clicked();

private:
    Ui::ChoiceDialog *ui;
};

#endif // 结束
