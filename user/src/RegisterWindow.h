#ifndef REGISTERWINDOW_H
#define REGISTERWINDOW_H

/**
 * @file RegisterWindow.h
 * 账号注册与头像裁剪流程。
 */

#include <QDebug>
#include <QMainWindow>
#include <QLabel>
#include <QTcpSocket>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QPainter>
#include <QPixmap>
#include <QBitmap>
#include <QPainterPath>
#include <QStandardItemModel>
#include <QBrush>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QFileDialog>
#include <QBuffer>
#include <QTextEdit>

namespace Ui {
class RegisterWindow;
}


class RegisterWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数
    explicit RegisterWindow(QWidget *parent = nullptr);
    // 析构函数
    ~RegisterWindow();

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);

private:
    // 初始化校验
    void setVal();
    // 初始化头像
    void setAva();

public slots:
    // 接收头像
    void receiveAvator(const QPixmap &pixmap);
    // 处理成功
    void regisSucceed(const QString &account);
    // 处理失败
    void regisFail();

private slots:
    // 输入变化
    void on_line_answer_textChanged(const QString &arg1);
    // 性别变化
    void on_cbbox_sex_currentIndexChanged(int index);
    // 密码变化
    void on_line_password_textChanged(const QString &arg1);
    // 确认变化
    void on_line_password2_textChanged(const QString &arg1);
    // 问题变化
    void on_line_question_textChanged(const QString &arg1);
    // 昵称变化
    void on_line_nickname_textChanged(const QString &arg1);
    // 点击提交
    void on_submit_but_clicked(bool checked);

signals:
    // 发送注册请求
    void sendRegis(const QJsonObject& ject);

private:
    Ui::RegisterWindow *ui;
    QRegularExpressionValidator *m_validator;
};

// 注册输入框
class LineRegis : public QLineEdit
{
    Q_OBJECT

public:
    LineRegis(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

// 注册头像控件
class LabelRegis : public QLabel
{
    Q_OBJECT

public:
    LabelRegis(QWidget *parent=nullptr):QLabel(parent){}
protected:
    // 点击头像
    void mousePressEvent(QMouseEvent *event);
};

#endif // 结束
