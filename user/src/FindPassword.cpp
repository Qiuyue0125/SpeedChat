/**
 * @file FindPassword.cpp
 * 找回密码三步流程界面。
 */
#include "FindPassword.h"
#include "ui_FindPassword.h"
#include "Dialog.h"

// 构造函数
FindPassword::FindPassword(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::FindPassword)
{
    ui->setupUi(this);
    // 初始化窗口
    setWindowFlags(Qt::Dialog);
    // 初始化校验
    setVal();
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
FindPassword::~FindPassword()
{
    delete ui;
    delete m_validator;
}

// 绘制窗口
void FindPassword::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPixmap pixmap(":/pictures/094 Cloudy Apple - trans.png");
    painter.drawPixmap(0, 0, width(), height(), pixmap);
    QMainWindow::paintEvent(event);
}

// 鼠标按下
void FindPassword::mousePressEvent(QMouseEvent *event)
{
    ui->line_pages1->clearFocus();
    ui->line_pages2->clearFocus();
    ui->line_pages3->clearFocus();
    ui->line_pages3_2->clearFocus();
    QMainWindow::mousePressEvent(event);
}

// 初始化校验
void FindPassword::setVal()
{
    QRegularExpression regExp("^[a-zA-Z0-9]{1,12}$");
    m_validator = new QRegularExpressionValidator(regExp, this);
    ui->line_pages1->setValidator(m_validator);
    ui->line_pages3->setValidator(m_validator);
    ui->line_pages3_2->setValidator(m_validator);
}

// 处理账号检查
void FindPassword::findPassword1(const QJsonObject &jsonObj)
{
    if(jsonObj["answer"] == "no"){
        QString text =  "请检查您的账号";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
    if(jsonObj["answer"] == "yes"){
        ui->page_find->setCurrentIndex(1);
        ui->lab_pages2->setText(jsonObj["question"].toString());
    }
}

// 处理答案检查
void FindPassword::findPassword2(const QJsonObject &jsonObj)
{
    if(jsonObj["answer"] == "no"){
        QString text =  "问题回答不正确";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
    if(jsonObj["answer"] == "yes"){
        ui->page_find->setCurrentIndex(2);
    }
}

// 处理重置结果
void FindPassword::findPassword3(const QJsonObject &jsonObj)
{
    if(jsonObj["answer"] == "no"){
        QString text =  "请求失败";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
    if(jsonObj["answer"] == "yes"){
        QString text =  "找回密码成功";
        Dialog* dialog = new Dialog(this->parentWidget());
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        this->close();
    }
}

// 点击第一步的下一步按钮
void FindPassword::on_but_pages1_clicked()
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "findpassword1";
    jsonObj["account_number"] = ui->line_pages1->text();
    emit sendForget(jsonObj);
}

// 点击第二步的下一步按钮
void FindPassword::on_but_pages2_clicked()
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "findpassword2";
    jsonObj["account_number"] = ui->line_pages1->text();
    jsonObj["theanswer"] = ui->line_pages2->text();
    emit sendForget(jsonObj);
}

// 点击提交
void FindPassword::on_but_pages3_clicked()
{
    if(ui->line_pages3->text() != ui->line_pages3_2->text()){
        QString text =  "两次密码不一致";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    QJsonObject jsonObj;
    jsonObj = QJsonObject();
    jsonObj["tag"] = "findpassword3";
    jsonObj["account_number"]= ui->line_pages1->text();
    jsonObj["theanswer"] = ui->line_pages2->text();
    jsonObj["password"] = ui->line_pages3_2->text();
    emit sendForget(jsonObj);
}

// 获取焦点
void LineFind::focusInEvent(QFocusEvent *event)
{
    if(text() == "请输入新的密码" || text() == "请确认密码"){
        setEchoMode(QLineEdit::Password);
    }
    setText("");
    setStyleSheet("font: 13pt 'Microsoft YaHei UI'; "
                  "border: 1px solid rgba(0, 0, 0, 0.3); "
                  "border-radius: 10px; "
                  "color: black;");
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineFind::focusOutEvent(QFocusEvent *event)
{
    if(text() == ""){
        setStyleSheet(
            "font: 13pt 'Microsoft YaHei UI'; "
            "border: 1px solid rgba(0, 0, 0, 0.3); "
            "border-radius: 8px; "
            "color: grey;"
            );
        setEchoMode(QLineEdit::Normal);
        if(objectName() == "line_pages1"){
            setText("请输入你的账号");
        }
        else if(objectName() == "line_pages2"){
            setText("请输入密保答案");
        }
        else if(objectName() == "line_pages3_2"){
            setText("请输入新的密码");
        }
        else if(objectName() == "line_pages3"){
            setText("请确认密码");
        }
    }
    QLineEdit::focusOutEvent(event);
}
