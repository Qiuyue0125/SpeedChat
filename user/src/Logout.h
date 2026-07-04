#ifndef LOGOUT_H
#define LOGOUT_H

/**
 * @file Logout.h
 * 注销确认对话框。
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
class Logout;
}

// 注销窗口
class Logout : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit Logout(QWidget *parent = nullptr);
    // 析构函数
    ~Logout();

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

private:
    // 初始化校验
    void setVal();

public slots:
    // 处理注销结果
    void logoutAnswer(const QJsonObject& json);

private slots:
    // 点击关闭
    void on_but_deletewindow_clicked();
    // 账号变化
    void on_line_account_textChanged(const QString &arg1);
    // 密码变化
    void on_line_password_textChanged(const QString &arg1);
    // 点击确认
    void on_but_yes_clicked();

signals:
    // 通知关闭
    void customClose();
    // 发送注销请求
    void logout(const QJsonObject &json);

private:
    Ui::Logout *ui;
    int m_moveFlag = 0;
    QPoint m_dragPosition;
};

// 注销输入框
class LineLogout : public QLineEdit
{
    Q_OBJECT

public:
    LineLogout(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

#endif // 结束
