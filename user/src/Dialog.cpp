/**
 * @file Dialog.cpp
 * 通用圆角提示框：文本、确定按钮显隐与用户可否关闭。
 */
#include "Dialog.h"
#include "ui_Dialog.h"
#include "AutoClearTextEdit.h"
#include <QCloseEvent>
#include <QKeyEvent>

// 构造函数
Dialog::Dialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Dialog)
{
    ui->setupUi(this);
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags( Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    // 初始化文本框
    setEdit();
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
Dialog::~Dialog()
{
    qDebug()<<"弹窗析构";
    delete ui;
}

// 设置文本
void Dialog::transText(const QString &text)
{
    ui->textEdit_notice->setText(text);
    ui->textEdit_notice->setAlignment(Qt::AlignCenter);
}

// 控制「确定」按钮显隐（如无按钮则忽略）
void Dialog::setConfirmButtonVisible(bool visible)
{
    if (ui->but_delete)
        ui->but_delete->setVisible(visible);
}

// 是否允许用户操作关闭路径（确定 / Esc / 关闭框）
void Dialog::setUserDismissEnabled(bool enabled)
{
    m_userDismissEnabled = enabled;
}

// 绘制窗口
void Dialog::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    int radius = 4;
    painter.setBrush(QBrush(QColor(255, 255, 255)));
    painter.drawRoundedRect(rect(), radius, radius);
    QDialog::paintEvent(event);
}

// 初始化文本框
void Dialog::setEdit()
{
    ui->textEdit_notice->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textEdit_notice->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textEdit_notice->setReadOnly(true);
    ui->textEdit_notice->setAlignment(Qt::AlignCenter);
}

// 点击按钮
void Dialog::on_but_delete_clicked()
{
    if (!m_userDismissEnabled)
        return;
    this->close();
}

// 关闭事件：禁止用户关闭时忽略事件
void Dialog::closeEvent(QCloseEvent *event)
{
    if (!m_userDismissEnabled) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

// 拒绝（如 Esc）：禁止用户关闭时不调用基类
void Dialog::reject()
{
    if (!m_userDismissEnabled)
        return;
    QDialog::reject();
}

// 键盘：禁止关闭时拦截 Esc
void Dialog::keyPressEvent(QKeyEvent *event)
{
    if (!m_userDismissEnabled && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}
