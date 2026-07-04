/**
 * @file AiAnalyzeResultDialog.cpp
 * 展示服务端返回的聊天 AI 分析结果。
 */
#include "AiAnalyzeResultDialog.h"
#include "ChatScrollStyle.h"
#include "ui_AiAnalyzeResultDialog.h"
#include "CloseButtonUtils.h"

#include <QIcon>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QScrollBar>

const QString AiAnalyzeResultDialog::BTN_ENABLE_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(5, 186, 251);
        color: white;
        border-radius: 10px;
        border: 0.5px solid rgb(220, 220, 220);
    }
    QPushButton:hover {
        background-color: rgba(5, 186, 251, 0.7);
        color: white;
        border-radius: 10px;
    }
    QPushButton:pressed {
        background-color: rgba(0, 123, 255, 0.8);
        color: rgba(255, 255, 255, 0.9);
        border-radius: 10px;
    }
)";

AiAnalyzeResultDialog::AiAnalyzeResultDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AiAnalyzeResultDialog)
{
    // 初始化无边框"AI 分析结果"对话框外观与滚动条样式。
    ui->setupUi(this);
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(false);

    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        const int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        const int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }

    // 直接给滚动条本体套用聊天界面同款样式（灰色、圆角）。
    if (ui->edit_result && ui->edit_result->verticalScrollBar()) {
        ui->edit_result->verticalScrollBar()->setStyleSheet(chatScrollBarVerticalStyleSheet());
    }

    ui->but_close->setStyleSheet(BTN_ENABLE_STYLE);
}

AiAnalyzeResultDialog::~AiAnalyzeResultDialog()
{
    delete ui;
}

// 设置展示的大段分析结果正文。
void AiAnalyzeResultDialog::setResultText(const QString &text)
{
    ui->edit_result->setPlainText(text);
}

// 绘制云背景与圆角外框。
void AiAnalyzeResultDialog::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPixmap background(":/pictures/094 Cloudy Apple - trans.png");
    if (background.isNull()) {
        qWarning("AiAnalyzeResultDialog: 背景图像加载失败");
        return;
    }
    const QSize newSize(this->width(), this->height());
    const QPixmap scaledBackground = background.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int x = (this->width() - scaledBackground.width()) / 2;
    const int y = (this->height() - scaledBackground.height()) / 2;
    const int radius = 10;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(x, y, scaledBackground);
    painter.setPen(QPen(QColor(255, 153, 179), 3.5));
    painter.drawRoundedRect(rect(), radius, radius);
}

// 鼠标按下：在边缘区域开始拖动窗口。
void AiAnalyzeResultDialog::mousePressEvent(QMouseEvent *event)
{
    for (QWidget *widget : findChildren<QWidget *>()) {
        widget->clearFocus();
    }
    const QPoint pos = event->pos();
    if (pos.x() <= 30 || pos.x() >= width() - 30 || pos.y() <= 30 || pos.y() >= height() - 30) {
        m_moveFlag = 1;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

// 鼠标移动：拖动窗口位置。
void AiAnalyzeResultDialog::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint globalPos = event->globalPosition().toPoint();
    if (event->buttons() & Qt::LeftButton) {
        if (m_moveFlag == 1) {
            move(globalPos - m_dragPosition);
            event->accept();
        }
        m_dragPosition = globalPos - frameGeometry().topLeft();
    }
    QDialog::mouseMoveEvent(event);
}

// 鼠标松开：结束拖动窗口状态。
void AiAnalyzeResultDialog::mouseReleaseEvent(QMouseEvent *event)
{
    QDialog::mouseReleaseEvent(event);
    m_moveFlag = 0;
}

// "x"关闭按钮：关闭对话框。
void AiAnalyzeResultDialog::on_but_deletewindow_clicked()
{
    close();
}

// "关闭"按钮：关闭对话框。
void AiAnalyzeResultDialog::on_but_close_clicked()
{
    close();
}
