#ifndef MAINWINDOWELSE_H
#define MAINWINDOWELSE_H

/**
 * @file MainWindowElse.h
 * MainWindow 拆分：部分成员与辅助声明。
 */

#include <QQueue>
#include <QWaitCondition>
#include <QThread>
#include <QStackedWidget>
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
#include <QMessageBox>
#include <QFileDialog>
#include <QBuffer>
#include <QMouseEvent>
#include <QTimer>
#include <QMutex>
#include <QJsonArray>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QMenu>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QListView>
#include <QPushButton>
#include <QSqlError>
#include <QDesktopServices>
#include <QClipboard>
#include <QDate>
#include <QDateTime>


class TalkFilterProxyModel;

// 消息列表
class TalkList : public QListView
{
    Q_OBJECT

public:
    TalkList(QWidget *parent = nullptr) : QListView(parent) {
    }

protected:
    // 菜单事件
    void contextMenuEvent(QContextMenuEvent *event);

signals:
    void choiceDone(const QJsonObject &json);
};

// 好友列表
class FriendList : public QListView
{
    Q_OBJECT

public:
    FriendList(QWidget *parent = nullptr) : QListView(parent) {
    }

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);
};

// 好友列表绘制
class FriendDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    FriendDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

protected:
    // 绘制列表项
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // 计算尺寸
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// 聊天列表绘制
class TalkDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    TalkDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

protected:
    // 绘制列表项
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // 计算尺寸
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    // 格式化时间
    QString displayTimeComparison(const QString &timestampStr) const;
};

// 好友列表过滤
class FilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
    }

protected:
    // 判断是否显示（好友列表过滤）
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const;
};

// 聊天列表过滤
class TalkFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit TalkFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
    }

protected:
    // 判断是否显示（聊天列表过滤）
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const;
};

// 搜索输入框
class LineSerach : public QLineEdit
{
    Q_OBJECT

public:
    LineSerach(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获取焦点
    void focusInEvent(QFocusEvent *event);
    // 失去焦点
    void focusOutEvent(QFocusEvent *event);
};

// 头像控件
class LabelAva : public QLabel
{
    Q_OBJECT

public:
    LabelAva(QWidget *parent=nullptr):QLabel(parent){}

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);

signals:
    void changeInfo();
};

// 好友头像控件
class LabelFriendAva : public QLabel
{
    Q_OBJECT

public:
    LabelFriendAva(QWidget *parent=nullptr, bool flag = true):QLabel(parent), m_flag(flag){}

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);

private:
    bool m_flag;
};

// 消息头像控件
class LabelFriendAvaInMessage : public QLabel
{
    Q_OBJECT

public:
    LabelFriendAvaInMessage(QWidget *parent=nullptr):QLabel(parent){}

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event);

signals:
    void showMessage();
};

// 信息文本框
class CustomTextEdit : public QTextEdit
{
public:
    CustomTextEdit(QWidget *parent = nullptr) : QTextEdit(parent) ,
        m_defaultBackground(":/pictures/tafei.png"), m_background(m_defaultBackground){}
    // 设置背景
    void setBackground(const QPixmap& pix);
    // 恢复背景
    void setDefaultBack();
    // 设置默认背景路径
    void setDefaultBackgroundPath(const QString &path);

protected:
    // 绘制背景
    void paintEvent(QPaintEvent *event);

private:
    QString m_defaultBackground;
    QPixmap m_background;
};

// 聊天页面容器
class TalkStacked : public QStackedWidget
{
public:
    TalkStacked(QWidget *parent = nullptr): QStackedWidget(parent), m_background(":/pictures/tafei.png"){};
    // 设置背景
    void setBackground(const QPixmap &pix);
    // 恢复默认背景
    void setDefaultBack();
    // 设置默认背景路径
    void setDefaultBackgroundPath(const QString &path);

protected:
    // 绘制背景
    void paintEvent(QPaintEvent *event);

private:
    QString m_defaultBackground;
    QPixmap m_background;
};

// 图片消息控件
class ImageLabel : public QLabel
{
    Q_OBJECT

public:
    // 图片消息控件构造函数
    explicit ImageLabel(const QString &pixmapPath, QWidget *parent = nullptr);

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event) override;
    // 菜单事件
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QString m_savePath;
};

// 语音消息控件
class AudioLabel : public QLabel
{
    Q_OBJECT

public:
    // 语音消息控件构造函数
    explicit AudioLabel(QString time ,const QString &audioPath, QWidget *parent = nullptr);

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event) override;

signals:
    void startAudio(const QString &audioPath);

private:
    QString m_audioPath;
};

// 消息输入框
class EnterTextEdit : public QTextEdit {
    Q_OBJECT

public:
    EnterTextEdit(QWidget *parent = nullptr) : QTextEdit(parent) {}

protected:
    // 键盘按下
    void keyPressEvent(QKeyEvent *event) override;

signals:
    // 回车信号
    void enterKey();
};

QDateTime parseFlexibleChatTimestamp(const QString& rawTimestamp);

// 一天四段：凌晨 / 上午 / 下午 / 晚上（按小时）
QString chineseDayPeriodForHour(int hour);
// 聊天用日期：今年仅「M月d日」，非今年带年份
QString formatChineseChatDateOnly(const QDate &date);

#endif // 结束
