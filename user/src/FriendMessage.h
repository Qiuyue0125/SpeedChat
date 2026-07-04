#ifndef FRIENDMESSAGE_H
#define FRIENDMESSAGE_H

/**
 * @file FriendMessage.h
 * 好友资料简要展示小窗。
 */

#include <QDialog>
#include <QLabel>
#include <QDialog>
#include <QString>
#include <QPainter>
#include <QBuffer>
#include <QPixmap>
#include <QFileDialog>
#include <QPainterPath>
#include <QJsonObject>
#include <QMouseEvent>

namespace Ui {
class FriendMessage;
}

class FriendMessage : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit FriendMessage(const QString& account, QWidget *parent = nullptr);
    // 析构函数
    ~FriendMessage();

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
    void setMessage(const QString &account);

private slots:
    // 点击关闭
    void on_but_cancelwindow_clicked();

private:
    Ui::FriendMessage *ui;
    int m_moveFlag = 0;
    QPoint m_dragPosition;
};

#endif // 结束
