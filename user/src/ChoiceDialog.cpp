/**
 * @file ChoiceDialog.cpp
 * 双按钮确认对话框（接受/拒绝）。
 */
#include "ChoiceDialog.h"
#include "ui_ChoiceDialog.h"
#include "AutoClearTextEdit.h"

// 构造函数
ChoiceDialog::ChoiceDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChoiceDialog)
{
    ui->setupUi(this);
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags( Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    // 初始化文本框
    setEdit();
    // 连接信号
    connect(ui->but_yes, &QPushButton::clicked, this, &ChoiceDialog::reject);
    connect(ui->but_no, &QPushButton::clicked, this, &ChoiceDialog::accept);
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
ChoiceDialog::~ChoiceDialog()
{
    qDebug()<<"选择弹窗析构";
    delete ui;
}

// 设置文本
void ChoiceDialog::transText(const QString &text)
{
    ui->textEdit_notice->setText(text);
    ui->textEdit_notice->setAlignment(Qt::AlignCenter);
}

// 设置按钮文本
void ChoiceDialog::transButText(const QString& no, const QString& yes)
{
    ui->but_no->setText(no);
    ui->but_yes->setText(yes);
}

// 绘制窗口
void ChoiceDialog::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    int radius = 4;
    painter.setBrush(QBrush(QColor(255, 255, 255)));
    painter.drawRoundedRect(rect(), radius, radius);
}

// 初始化文本框
void ChoiceDialog::setEdit()
{
    ui->textEdit_notice->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textEdit_notice->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textEdit_notice->setReadOnly(true);
    ui->textEdit_notice->setAlignment(Qt::AlignCenter);
}

// 点击确认
void ChoiceDialog::on_but_yes_clicked()
{
    close();
}

// 点击取消
void ChoiceDialog::on_but_no_clicked()
{
    close();
}
