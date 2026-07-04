/**
 * @file Logout.cpp
 * 注销确认对话框。
 */
#include "Logout.h"
#include "ui_Logout.h"
#include "Dialog.h"
#include "ChoiceDialog.h"
#include "CloseButtonUtils.h"

// 构造函数
Logout::Logout(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Logout)
{
    ui->setupUi(this);
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
    // 初始化校验
    setVal();
}

// 析构函数
Logout::~Logout()
{
    delete ui;
}

// 绘制窗口
void Logout::paintEvent(QPaintEvent *event)
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
void Logout::mousePressEvent(QMouseEvent *event)
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
void Logout::mouseMoveEvent(QMouseEvent *event)
{
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
void Logout::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    QDialog::mouseReleaseEvent(event);
}

// 关闭事件
void Logout::closeEvent(QCloseEvent *event)
{
    emit customClose();
    this->deleteLater();
    event->accept();
    QDialog::closeEvent(event);
}

// 初始化校验
void Logout::setVal()
{
    QRegularExpression regExp("^[a-zA-Z0-9]{1,12}$");
    QRegularExpression regExpNum("^[0-9]{1,12}$");
    QValidator *validator = new QRegularExpressionValidator(regExp, this);
    QValidator *validatorNum = new QRegularExpressionValidator(regExpNum, this);
    ui->line_account->setValidator(validatorNum);
    ui->line_password->setValidator(validator);
}

// 处理注销结果
void Logout::logoutAnswer(const QJsonObject& json)
{

    if(json["answer"] == "success"){
        QString text =  "注销成功:" + json["account"].toString();
        Dialog* dialog = new Dialog(this->parentWidget());
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        this->close();
    }
    else{
        QString text =  "注销失败,请检查账号密码";
        Dialog* dialog = new Dialog(this->parentWidget());
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}

// 点击关闭
void Logout::on_but_deletewindow_clicked()
{
    this->close();
}

// 账号变化
void Logout::on_line_account_textChanged(const QString &arg1)
{
    if(ui->line_account->text() != "" && ui->line_account->text() != "请输入您的账号"
        && ui->line_password->text() != "" && ui->line_password->text() != "请输入您的密码"){
        ui->but_yes->setEnabled(true);
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
    }
    else{
        ui->but_yes->setEnabled(false);
        ui->but_yes->setStyleSheet(
            "QPushButton {"
            "font: 12pt 'Microsoft YaHei UI';"
            "background-color: rgb(167, 214, 255); "
            "color: white;"
            "border-radius: 10px;"
            "}"
            );
    }
}

// 密码变化
void Logout::on_line_password_textChanged(const QString &arg1)
{
    if(ui->line_account->text() != "" && ui->line_account->text() != "请输入您的账号"
        && ui->line_password->text() != "" && ui->line_password->text() != "请输入您的密码"){
        ui->but_yes->setEnabled(true);
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
    }
    else{
        ui->but_yes->setEnabled(false);
        ui->but_yes->setStyleSheet(
            "QPushButton {"
            "font: 12pt 'Microsoft YaHei UI';"
            "background-color: rgb(167, 214, 255); "
            "color: white;"
            "border-radius: 10px;"
            "}"
            );
    }
}

// 点击确认
void Logout::on_but_yes_clicked()
{
    ChoiceDialog *dialog = new ChoiceDialog(this);
    dialog->transText("你确定永久注销此账号吗？");
    dialog->transButText("确定", "取消");
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    connect(dialog, &ChoiceDialog::accepted, this, [this]() {
        QJsonObject json;
        json["tag"] = "logout";
        json["account"] = ui->line_account->text();
        json["password"] = ui->line_password->text();
        emit logout(json);
    });
    dialog->show();
}

// 获取焦点
void LineLogout::focusInEvent(QFocusEvent *event)
{
    if(text() == "请输入您的账号" || text() == "请输入您的密码"){
        setText("");
        setStyleSheet("font: 12pt 'Microsoft YaHei UI';"
                      "border: 1px solid rgba(0, 0, 0, 0.1);"
                      "border-radius: 8px;"
                      "padding: 5px;"
                      "color: black;");
    }
    if(objectName() == "line_password"){
        this->setEchoMode(Password);
    }
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineLogout::focusOutEvent(QFocusEvent *event)
{
    if(text() == ""){
        if(objectName() == "line_accont")
        {
            setStyleSheet("font: 12pt 'Microsoft YaHei UI';"
                          "border: 1px solid rgba(0, 0, 0, 0.1);"
                          "border-radius: 8px;"
                          "padding: 5px;"
                          "color: grey;");
            setText("请输入您的账号");
        }
        if(objectName() == "line_password")
        {
            setStyleSheet("font: 12pt 'Microsoft YaHei UI';"
                          "border: 1px solid rgba(0, 0, 0, 0.1);"
                          "border-radius: 8px;"
                          "padding: 5px;"
                          "color: grey;");
            setText("请输入您的密码");
            setEchoMode(Normal);
        }
    }
    QLineEdit::focusOutEvent(event);
}
