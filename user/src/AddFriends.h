#ifndef ADDFRIENDS_H
#define ADDFRIENDS_H

/**
 * @file AddFriends.h
 * 搜索并添加好友。
 */

#include <QDialog>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QJsonObject>
#include <QTextEdit>
#include <QLabel>

namespace Ui {
class AddFriends;
}


class AddFriends : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit AddFriends(QWidget *parent = nullptr);
    // 析构函数
    ~AddFriends();

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
    // 处理搜索结果
    void searchResult(const QJsonObject &json);

private slots:
    // 点击关闭
    void on_but_deletewindow_clicked();
    // 回车搜索
    void on_line_search_returnPressed();
    // 点击搜索
    void on_but_search_clicked();

signals:
    // 发送搜索请求
    void searchFriends(const QJsonObject &jec);
    // 通知关闭
    void customClose();
    // 发送添加请求
    void addFriend(const QString &friendAccount);

private:
    Ui::AddFriends *ui;
    QString m_accountNum;
    QPoint m_dragPosition;
    int m_moveFlag = 0;
};

// 搜索输入框
class LineSerach_2 : public QLineEdit
{
    Q_OBJECT

public:
    LineSerach_2(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

#endif // 结束
