#ifndef FINDPASSWORD_H
#define FINDPASSWORD_H

/**
 * @file FindPassword.h
 * 找回密码三步流程界面。
 */

#include <QMainWindow>
#include <QTcpSocket>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QPainter>
#include <QPixmap>
#include <QBitmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QStyle>

namespace Ui {
class FindPassword;
}

class FindPassword : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数
    explicit FindPassword(QWidget *parent = nullptr);
    // 析构函数
    ~FindPassword();

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);

private:
    // 初始化校验
    void setVal();

public slots:
    // 处理账号检查
    void findPassword1(const QJsonObject &jsonObj);
    // 处理答案检查
    void findPassword2(const QJsonObject &jsonObj);
    // 处理重置结果
    void findPassword3(const QJsonObject &jsonObj);

private slots:
    // 点击第一步的下一步按钮
    void on_but_pages1_clicked();
    // 点击第二步的下一步按钮
    void on_but_pages2_clicked();
    // 点击提交
    void on_but_pages3_clicked();

signals:
    // 发送请求
    void sendForget(const QJsonObject& ject);

private:
    Ui::FindPassword *ui;
    QRegularExpressionValidator *m_validator;
};

// 找回输入框
class LineFind : public QLineEdit
{
    Q_OBJECT

public:
    LineFind(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

#endif // 结束
