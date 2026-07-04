/**
 * @file FriendMessage.cpp
 * 好友资料展示小窗。
 */
#include "FriendMessage.h"
#include "ui_FriendMessage.h"
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include "CloseButtonUtils.h"

// 构造函数
FriendMessage::FriendMessage(const QString & account, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FriendMessage)
{
    ui->setupUi(this);
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    // 关闭按钮统一样式
    CloseButtonUtils::setup(ui->but_cancelwindow, QIcon(":/pictures/icon_close_hover.png"));
    // 初始化信息
    setMessage(account);
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
FriendMessage::~FriendMessage()
{
    delete ui;
    this->disconnect();
    qDebug()<<"好友信息被析构";
}

// 绘制窗口
void FriendMessage::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 加载背景
    QPixmap background(":/pictures/094 Cloudy Apple - trans.png");
    if (background.isNull()) {
        qWarning("背景图像加载失败");
        return;
    }
    QSize newSize = QSize(this->width(), this->height());
    QPixmap scaledBackground = background.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    int x = (this->width() - scaledBackground.width()) / 2;
    int y = (this->height() - scaledBackground.height()) / 2;
    int radius = 10;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(x, y, scaledBackground);
    painter.setPen(QPen(QColor(255,153,179), 3.5));
    painter.drawRoundedRect(rect(), radius, radius);
    QDialog::paintEvent(event);
}

// 鼠标按下
void FriendMessage::mousePressEvent(QMouseEvent *event)
{
    // 清除焦点
    QList<QWidget*> widgets = this->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        widget->clearFocus();
    }
    QPoint pos = event->pos();
    if (pos.x() <= 30 || pos.x() >= width() - 30 ||
        pos.y() <= 30 || pos.y() >= height() - 30) {
        qDebug() << "点击在边缘";
        m_moveFlag = 1;
        m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

// 鼠标移动
void FriendMessage::mouseMoveEvent(QMouseEvent *event)
{
    qDebug()<<"鼠标位移了";
    QPoint globalPos = event->globalPosition().toPoint();
    if (event->buttons() & Qt::LeftButton) {
        if (m_moveFlag == 1) {
            this->move(globalPos - m_dragPosition);
            event->accept();
        }
        m_dragPosition = globalPos - this->frameGeometry().topLeft();
    }
    QDialog::mouseMoveEvent(event);
}

// 鼠标释放
void FriendMessage::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    QDialog::mouseReleaseEvent(event);
}

// 初始化信息
void FriendMessage::setMessage(const QString &account)
{
    QString name = AccountMessageManager::getInstance()->getInfo(account).name;
    QString signature = AccountMessageManager::getInstance()->getInfo(account).signature;
    QString gender = AccountMessageManager::getInstance()->getInfo(account).gender;

    ui->textEdit_account->setText(account);
    ui->textEdit_nickname->setText(name);
    ui->textEdit_sig->setText(signature);
    ui->textEdit_gender->setText(gender);
    QPixmap avator = AvatarManager::getInstance()->loadAvator(account, QSize(0,0));

    ui->lab_avator->setFixedSize(100, 100);
    ui->lab_avator->setPixmap(avator);
    ui->lab_avator->setStyleSheet(
        "QLabel {"
        "    border:none;"
        "    padding: 0px;"
        "}"
        );

    ui->lab_avator->setScaledContents(true);
    ui->lab_avator->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

// 点击关闭
void FriendMessage::on_but_cancelwindow_clicked()
{
    this->close();
    this->deleteLater();
}
