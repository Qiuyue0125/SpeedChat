/**
 * @file RegisterWindow.cpp
 * 账号注册窗口与头像裁剪流程。
 */
#include "RegisterWindow.h"
#include "ui_RegisterWindow.h"
#include "CutAvator.h"
#include "Dialog.h"

// 构造函数
RegisterWindow::RegisterWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::RegisterWindow)
{
    ui->setupUi(this);
    // 初始化窗口
    setWindowFlags(Qt::Dialog);
    // 初始化校验
    setVal();
    // 初始化头像
    setAva();
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
RegisterWindow::~RegisterWindow()
{
    delete ui;
}

// 绘制窗口
void RegisterWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPixmap pixmap(":/pictures/094 Cloudy Apple - trans.png");
    painter.drawPixmap(0, 0, width(), height(), pixmap);
    QMainWindow::paintEvent(event);
}

// 鼠标按下
void RegisterWindow::mousePressEvent(QMouseEvent *event)
{
    ui->line_answer->clearFocus();
    ui->line_nickname->clearFocus();
    ui->line_password->clearFocus();
    ui->line_password2->clearFocus();
    ui->line_question->clearFocus();
    QMainWindow::mousePressEvent(event);
}

// 初始化校验
void RegisterWindow::setVal()
{
    QRegularExpression regExp("^[a-zA-Z0-9]{1,12}$");
    RegisterWindow::m_validator = new QRegularExpressionValidator(regExp, this);
    ui->line_password->setValidator(m_validator);
    ui->line_password2->setValidator(m_validator);
}

// 初始化头像
void RegisterWindow::setAva()
{
    QPixmap pixmap(":/pictures/suliao_avator_normal.jpg");
    ui->lab_avator->setPixmap(pixmap);
    ui->lab_avator->setScaledContents(true);
}

// 接收头像
void RegisterWindow::receiveAvator(const QPixmap &pixmap)
{
    ui->lab_avator->setPixmap(pixmap);
    ui->lab_avator->setScaledContents(true);
}

// 处理成功
void RegisterWindow::regisSucceed(const QString &account)
{
    QString text =  "<p style='text-align: center;'>您的账号是:<br>" + account + "<br>请牢记!</p>";
    Dialog* dialog = new Dialog(this->parentWidget());
    dialog->transText(text);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    this->close();
}

// 处理失败
void RegisterWindow::regisFail()
{
    QString text =  "注册失败";
    Dialog* dialog = new Dialog(this);
    dialog->transText(text);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

// 输入变化
void RegisterWindow::on_line_answer_textChanged(const QString &arg1)
{
    if(!ui->line_nickname->text().isEmpty()&&!ui->line_password->text().isEmpty()\
        &&!ui->line_password2->text().isEmpty()&&!ui->line_question->text().isEmpty()\
        &&!ui->line_answer->text().isEmpty()&&ui->line_nickname->text() != "请输入昵称"\
        &&ui->line_password->text() !="请输入密码" &&ui->line_password2->text() != "请确认密码"\
        &&ui->line_question->text() !="请输入问题" &&ui->line_answer->text() !=" 请输入答案"){
        ui->submit_but->setEnabled(true);
        ui->submit_but->setCursor(Qt::PointingHandCursor);
        ui->submit_but->setStyleSheet(
            "QPushButton {"
            "    font: 12pt 'Microsoft YaHei UI';"
            "    background-color: rgb(5, 186, 251);"
            "    color: white;"
            "    border-radius: 15px;"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(5, 186, 251, 0.7);"
            "    color: white;"
            "    border-radius: 15px;"
            "}"
            "QPushButton:pressed {"
            "    background-color: rgba(0, 123, 255, 0.8);"
            "    color: rgba(255, 255, 255, 0.9);"
            "    border-radius: 15px;"
            "}"
            );}
    else{
        ui->submit_but->setEnabled(false);
        ui->submit_but->setCursor(Qt::ArrowCursor);
        ui->submit_but->setStyleSheet(
            "QPushButton {"
            "    font: 12pt 'Microsoft YaHei UI';"
            "    background-color: rgb(167, 214, 255);"
            "    color: white;"
            "    border-radius: 15px;"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(5, 186, 251, 0.7);"
            "    color: white;"
            "    border-radius: 15px;"
            "}"
            "QPushButton:pressed {"
            "    background-color: rgba(0, 123, 255, 0.8);"
            "    color: rgba(255, 255, 255, 0.9);"
            "    border-radius: 15px;"
            "}"
            );
    }
}

// 性别变化
void RegisterWindow::on_cbbox_sex_currentIndexChanged(int index)
{
    on_line_answer_textChanged("arg");
}

// 密码变化
void RegisterWindow::on_line_password_textChanged(const QString &arg1)
{
    on_line_answer_textChanged(arg1);
}

// 确认变化
void RegisterWindow::on_line_password2_textChanged(const QString &arg1)
{
    on_line_answer_textChanged(arg1);
}

// 问题变化
void RegisterWindow::on_line_question_textChanged(const QString &arg1)
{
    on_line_answer_textChanged(arg1);
}

// 昵称变化
void RegisterWindow::on_line_nickname_textChanged(const QString &arg1)
{
    on_line_answer_textChanged(arg1);
}

// 点击提交
void RegisterWindow::on_submit_but_clicked(bool checked)
{

    if(ui->line_password->text() != ui->line_password2->text()){
        QString text =  "您的两次密码不一致";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    qDebug() << "准备发送注册请求";
    QPixmap pixmap = ui->lab_avator->pixmap();
    if (pixmap.isNull()) {
        qDebug() << "头像不存在，无法发送";
        return;
    }
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    if (!buffer.open(QIODevice::WriteOnly)) {
        qDebug() << "无法打开缓冲区，用于保存pixmap";
        return;
    }
    if (!pixmap.save(&buffer, "PNG")) {
        qDebug() << "保存pixmap到缓冲区失败";
        return;
    }

    QString avator = byteArray.toBase64();
    ui->submit_but->setEnabled(false);
    QJsonObject jsonObj = QJsonObject();
    jsonObj["tag"] = "register";
    jsonObj["nickname"] = ui->line_nickname->text();
    jsonObj["gender"] = ui->cbbox_sex->currentText();
    jsonObj["password"] = ui->line_password->text();
    jsonObj["question"] = ui->line_question->text();
    jsonObj["answer"] = ui->line_answer->text();
    jsonObj["avator"] = avator;

    emit sendRegis(jsonObj);

    qDebug() << "发送注册请求";
}

// 获取焦点
void LineRegis::focusInEvent(QFocusEvent *event)
{
    if(this->text() == "请输入密码"||this->text() == "请确认密码"){
        this->setEchoMode(QLineEdit::Password);
    }
    setText("");
    setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                  "border: 1px solid rgba(0, 0, 0, 0.3); "
                  "border-radius: 10px; "
                  "color: black;");
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineRegis::focusOutEvent(QFocusEvent *event)
{
    if(text() == ""){
        setStyleSheet(
            "font: 12pt 'Microsoft YaHei UI'; "
            "border: 1px solid rgba(0, 0, 0, 0.3); "
            "border-radius: 8px; "
            "color: grey;"
            );
        if(objectName() == "line_nickname"){
            setText("请输入昵称");
        }
        else if(objectName() == "line_password"){
            setText("请输入密码");
            setEchoMode(QLineEdit::Normal);
        }
        else if(objectName() == "line_password2"){
            setText("请确认密码");
            setEchoMode(QLineEdit::Normal);
        }
        else if(objectName() == "line_question"){
            setText("请输入问题");
        }
        else if(objectName() == "line_answer"){
            setText("请输入答案");
        }
    }
    QLineEdit::focusOutEvent(event);
}

// 点击头像
void LabelRegis::mousePressEvent(QMouseEvent *event)
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
        connect(cutter,&CutAvator::cutOk,qobject_cast<RegisterWindow*>(this->parentWidget()->parentWidget()),&RegisterWindow::receiveAvator);
        cutter->setAttribute(Qt::WA_DeleteOnClose);
        cutter->show();
    }
    QLabel::mousePressEvent(event);
}
