/**
 * @file AddFriends.cpp
 * 搜索与添加好友对话框。
 */
#include "AddFriends.h"
#include <QStyle>
#include "ui_AddFriends.h"
#include "AutoClearTextEdit.h"
#include "Dialog.h"
#include "ChoiceDialog.h"
#include "AvatarManager.h"
#include "CloseButtonUtils.h"

// 构造函数
AddFriends::AddFriends(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AddFriends)
{
    ui->setupUi(this);
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    ui->but_search->setIcon(QIcon(":/pictures/icon_search.png"));
    ui->but_search->setIconSize(QSize(18, 18));
    // 初始化窗口
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags( Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    // 清除焦点
    ui->line_search->clearFocus();
    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
AddFriends::~AddFriends()
{
    delete ui;
    qDebug()<<"添加好友窗口析构函数执行";
}

// 绘制窗口
void AddFriends::paintEvent(QPaintEvent *event)
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
void AddFriends::mousePressEvent(QMouseEvent *event)
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
void AddFriends::mouseMoveEvent(QMouseEvent *event)
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
void AddFriends::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    QDialog::mouseReleaseEvent(event);
}

// 关闭事件
void AddFriends::closeEvent(QCloseEvent *event)
{
    emit customClose();
    this->deleteLater();
    event->accept();
    QDialog::closeEvent(event);
}

// 处理搜索结果
void AddFriends::searchResult(const QJsonObject &json)
{
    if(json["answer"] == "fail"){
        QString text =  "没有找到用户，请检查您输入的账号。";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
    else if(json["answer"] == "succeed"){
        // 清理旧内容
        QLayoutItem *item;
        while ((item = ui->layout_frame->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        // 添加间隔
        QSpacerItem *spacertop = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
        ui->layout_frame->addItem(spacertop);
        // 创建信息控件
        auto addTextEditWithStyle = [&](const QString &prefix, const QString &text) {
            AutoClearTextEdit *textEdit = new AutoClearTextEdit(prefix + text);
            textEdit->setContextMenuPolicy(Qt::NoContextMenu);
            textEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
            textEdit->setFixedSize(300, 42);
            textEdit->setStyleSheet(
                "font-size: 15px; "
                "color: #333333; "
                "font-weight: bold; "
                "padding: 5px; "
                "background: rgba(255, 255, 255 , 10); "
                "border: 1.5px solid black; "
                "border-radius: 5px;");
            textEdit->setAlignment(Qt::AlignCenter);
            textEdit->setReadOnly(true);
            textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            return textEdit;
        };
        // 读取信息
        m_accountNum = json["account_number"].toString();
        QString nickname = json["nickname"].toString();
        QString gender = json["gender"].toString();
        QString birthday = json["birthday"].toString();
        QString signature = json["signature"].toString();
        QString avator = json["avator"].toString();
        QByteArray byteArray = QByteArray::fromBase64(avator.toUtf8());
        QImage image;
        image.loadFromData(byteArray);
        QPixmap pixmap = QPixmap::fromImage(image);
        // 显示头像
        QLabel *avatorLabel = new QLabel;
        int actualSize = 120 - 6;
        QPixmap scaledPixmap = pixmap.scaled(actualSize, actualSize,
                                             Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);

        QPixmap avatarPixmap = AvatarManager::getRoundedPixmap(scaledPixmap, 11);

        avatorLabel->setPixmap(avatarPixmap);
        avatorLabel->setFixedSize(120, 120);
        avatorLabel->setAlignment(Qt::AlignCenter);
        avatorLabel->setStyleSheet(
            "QLabel {"
            "    border: 3px solid #3498db;"
            "    border-radius: 15px;"
            "    background-color: white;"
            "}"
            );
        ui->layout_frame->addWidget(avatorLabel, 0, Qt::AlignCenter);
        // 添加间隔
        QSpacerItem *spacer0 = new QSpacerItem(0, 3, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->layout_frame->addItem(spacer0);
        // 添加信息
        if (!nickname.isEmpty()) ui->layout_frame->addWidget(addTextEditWithStyle("昵称: ", nickname), 0, Qt::AlignCenter);
        if (!m_accountNum.isEmpty()) ui->layout_frame->addWidget(addTextEditWithStyle("账号: ", m_accountNum), 0, Qt::AlignCenter);
        if (!gender.isEmpty()) ui->layout_frame->addWidget(addTextEditWithStyle("性别: ", gender), 0, Qt::AlignCenter);
        if (!birthday.isEmpty()) ui->layout_frame->addWidget(addTextEditWithStyle("生日: ", birthday), 0, Qt::AlignCenter);
        if (!signature.isEmpty()) ui->layout_frame->addWidget(addTextEditWithStyle("签名: ", signature), 0, Qt::AlignCenter);
        // 添加间隔
        QSpacerItem *spacer1 = new QSpacerItem(0, 3, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->layout_frame->addItem(spacer1);
        // 添加按钮
        QPushButton *addButton = new QPushButton("添加好友");
        addButton->setCursor(Qt::PointingHandCursor);
        addButton->setFixedSize(180, 55);
        addButton->setStyleSheet(
            "QPushButton {"
            "    font: 12pt 'Microsoft YaHei UI';"
            "    background-color: rgb(5, 186, 251);"
            "    color: white;"
            "    font-weight: bold; "
            "    border: none;"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(5, 186, 251, 0.7);"
            "    font-weight: bold; "
            "    color: white;"
            "    border: none;"
            "}"
            "QPushButton:pressed {"
            "    background-color: rgba(0, 123, 255, 0.8);"
            "    font-weight: bold; "
            "    color: rgba(255, 255, 255, 0.9);"
            "    border: none;"
            "}"
            );
        ui->layout_frame->addWidget(addButton, 0, Qt::AlignCenter);
        ui->layout_frame->setSpacing(3);
        ui->layout_frame->setContentsMargins(5, 5, 5, 5);
        QSpacerItem *spacer2 = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
        ui->layout_frame->addItem(spacer2);
        addButton->setFocusPolicy(Qt::ClickFocus);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            ChoiceDialog *dialog = new ChoiceDialog(this);
            dialog->transText("你确定要添加好友吗?");
            dialog->transButText("确定", "取消");
            dialog->setAttribute(Qt::WA_DeleteOnClose);

            connect(dialog, &ChoiceDialog::accepted, this, [this]() {
                emit addFriend(m_accountNum);
            });

            dialog->show();
        });
    }
}

// 点击关闭
void AddFriends::on_but_deletewindow_clicked()
{
    this->close();
}

// 回车搜索
void AddFriends::on_line_search_returnPressed()
{
    if(ui->line_search->text() == "输入对方账号搜索" || ui->line_search->text().length() != 10){
        QString text =  "请输入正确的账号。";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    QJsonObject jsonObj;
    jsonObj["tag"] = "searchfriend";
    jsonObj["account"] = ui->line_search->text();
    emit searchFriends(jsonObj);
    ui->line_search->clearFocus();
}

// 点击搜索
void AddFriends::on_but_search_clicked()
{
    on_line_search_returnPressed();
}

// 获取焦点
void LineSerach_2::focusInEvent(QFocusEvent *event)
{
    setStyleSheet("font: 450 11pt 'Microsoft YaHei UI Light'; "
                  "border: 1px solid rgba(0, 0, 0, 0.3); "
                  "border-radius: 5px; "
                  "border: none;"
                  "padding:5px;"
                  "color: black;");
    if(text() == "输入对方账号搜索"){
        setText("");
    }
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineSerach_2::focusOutEvent(QFocusEvent *event)
{

    if (text().isEmpty()) {
        setText("输入对方账号搜索");
        setStyleSheet("font: 400 11pt 'Microsoft YaHei UI Light'; "
                      "border: 1px solid rgba(0, 0, 0, 0.3); "
                      "border-radius: 5px; "
                      "border: none;"
                      "padding:5px;"
                      "color: grey;");
    }
    else{
        setStyleSheet("font: 450 11pt 'Microsoft YaHei UI Light'; "
                      "border: 1px solid rgba(0, 0, 0, 0.3); "
                      "border-radius: 5px; "
                      "border: none;"
                      "padding:5px;"
                      "color: grey;");
    }
    QLineEdit::focusOutEvent(event);
}
