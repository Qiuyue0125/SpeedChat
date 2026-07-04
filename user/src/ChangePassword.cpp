/**
 * @file ChangePassword.cpp
 * 修改密码两步流程对话框。
 */
#include "ChangePassword.h"
#include "ui_ChangePassword.h"
#include "Dialog.h"
#include "CloseButtonUtils.h"

// 构造函数
ChangePassword::ChangePassword(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChangePassword)
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
ChangePassword::~ChangePassword()
{
    delete ui;
}

// 初始化校验
void ChangePassword::setVal()
{
    QRegularExpression regExp("^[a-zA-Z0-9]{1,12}$");
    QRegularExpression regExpNum("^[0-9]{1,12}$");
    QValidator *validator = new QRegularExpressionValidator(regExp, this);
    QValidator *validatorNum = new QRegularExpressionValidator(regExpNum, this);
    ui->line_account->setValidator(validatorNum);
    ui->line_password->setValidator(validator);
    ui->line_newpassword->setValidator(validator);
    ui->line_newpassword2->setValidator(validator);
}

// 绘制窗口
void ChangePassword::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);
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
}

// 鼠标按下
void ChangePassword::mousePressEvent(QMouseEvent *event)
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
void ChangePassword::mouseMoveEvent(QMouseEvent *event)
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
void ChangePassword::mouseReleaseEvent(QMouseEvent *event)
{
    QDialog::mouseReleaseEvent(event);
    m_moveFlag = 0;
}

// 关闭事件
void ChangePassword::closeEvent(QCloseEvent *event)
{
    emit customClose();
    this->deleteLater();
    event->accept();
    QDialog::closeEvent(event);
}

// 点击关闭
void ChangePassword::on_but_deletewindow_clicked()
{
    this->close();
}

const QString ChangePassword::BTN_ENABLE_STYLE = R"(
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

const QString ChangePassword::BTN_DISABLE_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(167, 214, 255);
        color: white;
        border-radius: 10px;
    }
)";

// 处理第一步结果
void ChangePassword::changePasswordAns1(const QJsonObject& json)
{
    if(json ["answer"] == "fail"){
        ui->line_account->setEnabled(true);
        ui->line_password->setEnabled(true);
        ui->but_yes->setEnabled(true);
        QString text =  "请求失败。";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    else if(json ["answer"] == "user_not_found"){
        ui->line_account->setEnabled(true);
        ui->line_password->setEnabled(true);
        ui->but_yes->setEnabled(true);
        QString text =  "账号或密码错误，请检查。";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    else if(json ["answer"] == "succeed"){
        ui->stackedWidget->setCurrentIndex(1);
    }
}

// 处理第二步结果
void ChangePassword::changePasswordAns2(const QJsonObject& json)
{
    if(json ["answer"] == "fail"){
        QString text =  "修改失败。";
        Dialog* dialog = new Dialog(this->parentWidget());
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        this->close();
        return;
    }
    else if(json ["answer"] == "succeed"){
        QString text =  "修改成功!";
        Dialog* dialog = new Dialog(this->parentWidget());
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        this->close();
        return;
    }
}

// 账号变化
void ChangePassword::on_line_account_textChanged(const QString &arg1)
{
    if(ui->line_account->text() != "" && ui->line_account->text() != "请输入您的账号"
        && ui->line_password->text() != "" && ui->line_password->text() != "请输入您的密码"){
        ui->but_yes->setEnabled(true);
        ui->but_yes->setStyleSheet(BTN_ENABLE_STYLE);
    }
    else{
        ui->but_yes->setEnabled(false);
        ui->but_yes->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 密码变化
void ChangePassword::on_line_password_textChanged(const QString &arg1)
{
    if(ui->line_account->text() != "" && ui->line_account->text() != "请输入您的账号"
        && ui->line_password->text() != "" && ui->line_password->text() != "请输入您的密码"){
        ui->but_yes->setEnabled(true);
        ui->but_yes->setStyleSheet(BTN_ENABLE_STYLE);
    }
    else{
        ui->but_yes->setEnabled(false);
        ui->but_yes->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 新密码变化
void ChangePassword::on_line_newpassword_textChanged(const QString &arg1)
{
    if(ui->line_newpassword->text() != "" && ui->line_newpassword->text() != "请输入新的密码"
        && ui->line_newpassword2->text() != "" && ui->line_newpassword2->text() != "请确认您的密码"){
        ui->but_yes_2->setEnabled(true);
        ui->but_yes_2->setStyleSheet(BTN_ENABLE_STYLE);
    }
    else{
        ui->but_yes_2->setEnabled(false);
        ui->but_yes_2->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 确认变化
void ChangePassword::on_line_newpassword2_textChanged(const QString &arg1)
{
    if(ui->line_newpassword->text() != "" && ui->line_newpassword->text() != "请输入新的密码"
        && ui->line_newpassword2->text() != "" && ui->line_newpassword2->text() != "请确认您的密码"){
        ui->but_yes_2->setEnabled(true);
        ui->but_yes_2->setStyleSheet(BTN_ENABLE_STYLE);
    }
    else{
        ui->but_yes_2->setEnabled(false);
        ui->but_yes_2->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 点击下一步
void ChangePassword::on_but_yes_clicked()
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "changepassword1";
    jsonObj["account"] = ui->line_account->text();
    jsonObj["password"] = ui->line_password->text();
    emit changePassword1(jsonObj);
    ui->line_account->setEnabled(false);
    ui->line_password->setEnabled(false);
    ui->but_yes->setEnabled(false);
}

// 点击确认
void ChangePassword::on_but_yes_2_clicked()
{
    if(ui->line_newpassword->text() != ui->line_newpassword2->text()){
        QString text =  "您的两次密码不一致。";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    QJsonObject jsonObj;
    jsonObj["tag"] = "changepassword2";
    jsonObj["account"] = ui->line_account->text();
    jsonObj["password"] = ui->line_password->text();
    jsonObj["newpassword"] = ui->line_newpassword->text();
    emit changePassword2(jsonObj);
    ui->line_newpassword->setEnabled(false);
    ui->line_newpassword2->setEnabled(false);
    ui->but_yes_2->setEnabled(false);
}

// 获取焦点
void LineChangePass::focusInEvent(QFocusEvent *event)
{
    setText("");
    setStyleSheet("font: 12pt 'Microsoft YaHei UI';"
                  "border: 1px solid rgba(0, 0, 0, 0.1);"
                  "border-radius: 8px;"
                  "padding: 5px;"
                  "color: black;");
    if(objectName() == "line_newpassword" || objectName() == "line_newpassword2" || objectName() == "line_password"){
        this->setEchoMode(Password);
    }
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineChangePass::focusOutEvent(QFocusEvent *event)
{
    if(text() == ""){
        setStyleSheet("font: 12pt 'Microsoft YaHei UI';"
                      "border: 1px solid rgba(0, 0, 0, 0.1);"
                      "border-radius: 8px;"
                      "padding: 5px;"
                      "color: grey;");
        if(objectName() == "line_accont")
        {
            setText("请输入您的账号");
        }
        if(objectName() == "line_password")
        {
            setText("请输入您的密码");
            setEchoMode(Normal);
        }
        if(objectName() == "line_newpassword")
        {
            setText("请输入新的密码");
            setEchoMode(Normal);
        }
        if(objectName() == "line_newpassword2")
        {
            setText("请确认您的密码");
            setEchoMode(Normal);
        }
    }
    QLineEdit::focusOutEvent(event);
}
