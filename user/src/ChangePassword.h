#ifndef CHANGEPASSWORD_H
#define CHANGEPASSWORD_H

/**
 * @file ChangePassword.h
 * 修改密码两步验证对话框。
 */

#include <QValidator>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QLineEdit>
#include <QJsonObject>

namespace Ui {
class ChangePassword;
}

class ChangePassword : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit ChangePassword(QWidget *parent = nullptr);
    // 析构函数
    ~ChangePassword();

private:
    // 初始化校验
    void setVal();

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);
    // 鼠标移动
    void mouseMoveEvent(QMouseEvent *event);
    // 鼠标释放
    void mouseReleaseEvent(QMouseEvent *event);
    // 关闭事件
    void closeEvent(QCloseEvent *event);

public slots:
    // 处理第一步结果
    void changePasswordAns1(const QJsonObject& json);
    // 处理第二步结果
    void changePasswordAns2(const QJsonObject& json);

private slots:
    // 点击关闭
    void on_but_deletewindow_clicked();
    // 账号变化
    void on_line_account_textChanged(const QString &arg1);
    // 密码变化
    void on_line_password_textChanged(const QString &arg1);
    // 新密码变化
    void on_line_newpassword_textChanged(const QString &arg1);
    // 确认变化
    void on_line_newpassword2_textChanged(const QString &arg1);
    // 点击下一步
    void on_but_yes_clicked();
    // 点击确认
    void on_but_yes_2_clicked();

signals:
    // 通知关闭
    void customClose();
    // 发送第一步请求
    void changePassword1(const QJsonObject &json);
    // 发送第二步请求
    void changePassword2(const QJsonObject &json);

private:
    Ui::ChangePassword *ui;
    int m_moveFlag = 0;
    QPoint m_dragPosition;
    static const QString BTN_ENABLE_STYLE;
    static const QString BTN_DISABLE_STYLE;
};

// 修改输入框
class LineChangePass : public QLineEdit
{
    Q_OBJECT

public:
    LineChangePass(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

#endif // 结束
