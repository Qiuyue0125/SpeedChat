#ifndef DIALOG_H
#define DIALOG_H

/**
 * @file Dialog.h
 * 半透明衬底的通用对话框基类。
 */

#include <QDialog>
#include <QString>
#include <QPainter>

class QCloseEvent;
class QKeyEvent;
class QPaintEvent;

namespace Ui {
class Dialog;
}


class Dialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit Dialog(QWidget *parent = nullptr);
    // 析构函数
    ~Dialog();
    // 设置文本
    void transText(const QString &text);
    // 是否显示「确定」按钮（如网络重连提示需隐藏，仅允许代码关闭）
    void setConfirmButtonVisible(bool visible);
    // 是否允许用户通过确定 / Esc / 关闭 关掉对话框（为 false 时仅可代码侧先 setUserDismissEnabled(true) 再 close）
    void setUserDismissEnabled(bool enabled);
    // 当前是否允许用户关闭（与 setUserDismissEnabled 成对使用）
    bool userDismissEnabled() const { return m_userDismissEnabled; }

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event) override;
    // 系统/标题栏关闭时：禁止关闭则 ignore
    void closeEvent(QCloseEvent *event) override;
    // Esc 触发的拒绝：禁止关闭时不调用基类 reject
    void reject() override;
    // Esc 键：禁止关闭时不交给基类处理
    void keyPressEvent(QKeyEvent *event) override;

private:
    // 初始化文本框
    void setEdit();

private slots:
    // 点击按钮
    void on_but_delete_clicked();

private:
    Ui::Dialog *ui;
    bool m_userDismissEnabled = true;
};

#endif // 结束
