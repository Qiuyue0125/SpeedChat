/**
 * @file ChangeInformation.cpp
 * 修改个人资料对话框。
 */
#include "ChangeInformation.h"
#include "ui_ChangeInformation.h"
#include "Dialog.h"
#include "CutAvator.h"
#include "AccountMessageManager.h"
#include "AvatarManager.h"
#include "CloseButtonUtils.h"
#include <QJsonArray>
#include <QPainterPath>

// 构造函数
ChangeInformation::ChangeInformation(const AccountInfo& info, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChangeInformation)
{
    ui->setupUi(this);
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    m_actInfo = info;
    // 关闭按钮统一样式
    CloseButtonUtils::setup(ui->but_cancelwindow, QIcon(":/pictures/icon_close_hover.png"));
    // 初始化信息
    setMessage(m_actInfo);
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
ChangeInformation::~ChangeInformation()
{
    delete ui;
    qDebug()<<"修改个人信息窗口析构函数被调用";
    disconnect();
}

// 绘制窗口
void ChangeInformation::paintEvent(QPaintEvent *event)
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
void ChangeInformation::mousePressEvent(QMouseEvent *event)
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
void ChangeInformation::mouseMoveEvent(QMouseEvent *event)
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
void ChangeInformation::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    QDialog::mouseReleaseEvent(event);
}

// 初始化信息
void ChangeInformation::setMessage(const AccountInfo &info)
{
    const QString genders[] = {"男", "女", "其他"};
    if(info.gender == "女"){
        ui->but_sex->setText("女");
    }
    else if(info.gender == "其他"){
        ui->but_sex->setText("其他");
    }
    else{
        ui->but_sex->setText("男");
        if(info.gender.isEmpty()) m_actInfo.gender = "男";
    }
    ui->textEdit_account->setText(info.account);
    ui->line_nickname->setText(info.name);
    ui->line_sig->setText(info.signature);
    QPixmap avator = AvatarManager::getInstance()->loadAvator(info.account, QSize(0,0));
    ui->lab_avator->setScaledContents(true);
    ui->lab_avator->setPixmap(avator);
}

// 更新按钮状态
void ChangeInformation::judgeMessage()
{
    int flag = 0;
    if(m_avaFlag == 1){
        flag = 1;
    }
    if(ui->line_nickname->text() != m_actInfo.name || ui->line_sig->text() != m_actInfo.signature || ui->but_sex->text() != m_actInfo.gender){
        flag = 1;
    }
    if(ui->line_nickname->text() == ""){
        flag = 0;
    }

    if(flag == 0){
        ui->but_yes->setStyleSheet(
            "QPushButton {"
            "font: 12pt 'Microsoft YaHei UI';"
            "background-color: rgb(167, 214, 255); "
            "color: white;"
            "border-radius: 10px;"
            "}"
            );
        ui->but_yes->setEnabled(false);
    }
    else{
        ui->but_yes->setStyleSheet(
            "QPushButton {"
            "font: 12pt 'Microsoft YaHei UI';"
            "background-color: rgb(5, 186, 251);"
            "color: white;"
            "border-radius: 10px;"
            "border: 0.5px solid rgb(220, 220, 220);"
            "}"
            "QPushButton:hover {"
            "background-color: rgba(5, 186, 251, 0.7);"
            "color: white;"
            "border-radius: 10px;"
            "}"
            "QPushButton:pressed {"
            "background-color: rgba(0, 123, 255, 0.8);"
            "color: rgba(255, 255, 255, 0.9);"
            "border-radius: 10px;"
            "}"
            );
        ui->but_yes->setEnabled(true);
    }
}

// 接收头像
void ChangeInformation::getAva(const QPixmap &pixmap)
{
    ui->lab_avator->setPixmap(pixmap);
    m_avaFlag = 1;
    judgeMessage();
}

// 处理结果
void ChangeInformation::dealResult(const QJsonObject &json)
{
    if(json["answer"] == "succeed"){
        emit customClose();
        close();
    }
    else if(json["answer"] == "fail"){
        qDebug()<<"修改资料失败";
        QString text = "修改失败!";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        judgeMessage();
    }
}

// 点击关闭
void ChangeInformation::on_but_cancelwindow_clicked()
{
    emit customClose();
    this->close();
    this->deleteLater();
}

// 点击取消
void ChangeInformation::on_but_no_clicked()
{
    on_but_cancelwindow_clicked();
}

// 点击性别按钮 — 循环切换：男→女→其他→男
void ChangeInformation::on_but_sex_clicked()
{
    const QString genders[] = {"男", "女", "其他"};
    const QString cur = ui->but_sex->text();
    int idx = 0;
    for (int i = 0; i < 3; ++i) {
        if (cur == genders[i]) { idx = i; break; }
    }
    ui->but_sex->setText(genders[(idx + 1) % 3]);
    judgeMessage();
}

// 昵称变化
void ChangeInformation::on_line_nickname_textChanged(const QString &arg1)
{
    judgeMessage();
}

// 签名变化
void ChangeInformation::on_line_sig_textChanged(const QString &arg1)
{
    judgeMessage();
}

// 点击确认
void ChangeInformation::on_but_yes_clicked()
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "changeinformation";
    jsonObj["account"] = ui->textEdit_account->toPlainText();
    jsonObj["nickname"] = ui->line_nickname->text();
    jsonObj["gender"] = ui->but_sex->text();
    jsonObj["signature"] = ui->line_sig->text();
    jsonObj["avator"] = AvatarManager::pixmapToBase64(ui->lab_avator->pixmap());

    QList<QString> keys = AccountMessageManager::getInstance()->getKeys();
    QJsonArray friendKeysArray;
    for (const QString& key : keys) {
        friendKeysArray.append(key);
    }
    jsonObj["friendIds"] = friendKeysArray;

    emit sendMessage(jsonObj);
    ui->but_yes->setEnabled(false);
}

// 获取焦点
void LineMessage::focusInEvent(QFocusEvent *event)
{
    if(m_flag == 0){
        setText("");
        m_flag = 1;
    }
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineMessage::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
}

// 点击头像
void LabelChooseAva::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "点击头像了";
    QString filename = QFileDialog::getOpenFileName(nullptr, "选择图片", "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (!filename.isEmpty()) {
        qDebug() << "头像路径有效";
        QPixmap pixmap(filename);
        if (pixmap.isNull()) {
            qDebug() << "加载图片失败";
            return;
        }
        // 打开裁剪窗口
        CutAvator *cutter = new CutAvator(filename);
        cutter->setWindowFlags(Qt::WindowStaysOnTopHint|Qt::Dialog);
        connect(cutter,&CutAvator::cutOk,qobject_cast<ChangeInformation*>(this->parentWidget()->parentWidget()),&ChangeInformation::getAva);
        cutter->setAttribute(Qt::WA_DeleteOnClose);
        cutter->show();
    }
    QLabel::mousePressEvent(event);
}
