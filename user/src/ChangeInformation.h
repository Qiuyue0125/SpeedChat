#ifndef CHANGEINFORMATION_H
#define CHANGEINFORMATION_H

/**
 * @file ChangeInformation.h
 * 修改个人资料对话框。
 */

#include <QLineEdit>
#include <QLabel>
#include <QDialog>
#include <QString>
#include <QPainter>
#include <QBuffer>
#include <QPixmap>
#include <QFileDialog>
#include <QJsonObject>
#include "AccountMessageManager.h"

namespace Ui {
class ChangeInformation;
}

struct AccountInfo;

class ChangeInformation : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit ChangeInformation(const AccountInfo& info, QWidget *parent = nullptr);
    // 析构函数
    ~ChangeInformation();

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);
    // 鼠标移动
    void mouseMoveEvent(QMouseEvent *event);
    // 鼠标释放
    void mouseReleaseEvent(QMouseEvent *event);

private:
    // 初始化信息
    void setMessage(const AccountInfo &info);
    // 更新按钮状态
    void judgeMessage();

public slots:
    // 接收头像
    void getAva(const QPixmap &pixmap);
    // 处理结果
    void dealResult(const QJsonObject &json);

private slots:
    // 点击关闭
    void on_but_cancelwindow_clicked();
    // 点击取消
    void on_but_no_clicked();
    // 点击性别按钮
    void on_but_sex_clicked();
    // 昵称变化
    void on_line_nickname_textChanged(const QString &arg1);
    // 签名变化
    void on_line_sig_textChanged(const QString &arg1);
    // 点击确认
    void on_but_yes_clicked();

signals:
    // 通知关闭
    void customClose();
    // 发送修改请求
    void sendMessage(const QJsonObject &json);

private:
    Ui::ChangeInformation *ui;
    AccountInfo m_actInfo;
    QPoint m_dragPosition;
    int m_moveFlag = 0;
    int m_avaFlag = 0;
};

// 信息输入框
class LineMessage : public QLineEdit
{
    Q_OBJECT

public:
    LineMessage(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);

private:
    int m_flag = 0;
};

// 头像控件
class LabelChooseAva : public QLabel
{
    Q_OBJECT

public:
    LabelChooseAva(QWidget *parent=nullptr):QLabel(parent){}
    // 点击头像
    void mousePressEvent(QMouseEvent *event);
};
#endif // 结束
