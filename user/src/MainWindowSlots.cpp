/**
 * @file MainWindowSlots.cpp
 * 主窗口槽函数：发送消息、好友/聊天列表交互、音视频与文件发送等。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AddFriends.h"
#include "ChangeInformation.h"
#include "ChoiceDialog.h"
#include "Dialog.h"
#include "CloudDrive.h"
#include "FriendMessage.h"
#include "AutoClearTextEdit.h"
#include "SocketOnly.h"
#include "SocketDoc.h"
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include "MainWindowElse.h"
#include "AiAnalyzeDialog.h"
#include <QDialog>
#include <QTimer>
#include <QRegularExpression>
#include <QFileDialog>
#include <QBuffer>
#include <QPixmap>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>
#include <QMediaDevices>
#include <QAudioSource>
#include <QAudio>
#include <QSpacerItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QImage>
#include <QCursor>
#include <QSqlQuery>
#include <QMetaObject>
#include <QCryptographicHash>

// 点击主窗口最小化按钮
void MainWindow::on_but_minwindow_clicked()
{
    setWindowState(windowState() | Qt::WindowMinimized);
}

// 点击主窗口最大化按钮
void MainWindow::on_but_maxwindow_clicked()
{
    if(m_maxFlag == 0){
        this->showMaximized();
        m_maxFlag = 1;
    }
    else{
        this->showNormal();
        m_maxFlag = 0;
    }
    QTimer::singleShot(0, this, [this]() {
    });
}

// 点击主窗口关闭按钮
void MainWindow::on_but_deletewindow_clicked()
{
    // 顶部关闭按钮：默认最小化到托盘；真正退出由左下角菜单“退出账号”或托盘“退出”触发。
    close();
}

// 跳转到聊天窗口页面
void MainWindow::on_but_chat_clicked()
{
    ui->pages->setCurrentIndex(0);
}

// 跳转到联系人页面
void MainWindow::on_but_friends_clicked()
{
    ui->pages->setCurrentIndex(1);
}

// 切换到云盘页面
void MainWindow::on_but_cloud_clicked()
{
    if (ui->pages->currentIndex() != 2) {
        // 首次切换时刷新我的上传列表
        if (m_cloudDrive) {
            m_cloudDrive->refreshMyFilesList();
        }
    }
    ui->pages->setCurrentIndex(2);
}

// 切换到ai助手页面
void MainWindow::on_but_ai_clicked()
{
    ui->pages->setCurrentIndex(3);
}

// 输入框改变内容判断能否发送消息
void MainWindow::on_edit_input_textChanged()
{
    refreshSendButtonState();
}

// 发送按钮已移除，保留空函数体避免调用处报错
void MainWindow::refreshSendButtonState()
{
}

// 页面发生改变
void MainWindow::on_pages_currentChanged(int arg1)
{
    if (arg1 == 0) {
        ui->but_chat->setStyleSheet(BTN_SELECTED_STYLE_SEC);
        ui->but_friends->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_cloud->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_ai->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
    }
    if (arg1 == 1) {
        ui->but_chat->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_friends->setStyleSheet(BTN_SELECTED_STYLE_SEC);
        ui->but_cloud->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_ai->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
    }
    if (arg1 == 2) {
        ui->but_chat->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_friends->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_cloud->setStyleSheet(BTN_SELECTED_STYLE_SEC);
        ui->but_ai->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
    }
    if (arg1 == 3) {
        ui->but_chat->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_friends->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_cloud->setStyleSheet(BTN_UNSELECTED_STYLE_SEC);
        ui->but_ai->setStyleSheet(BTN_SELECTED_STYLE_SEC);
    }
}

// 点击添加好友按钮（主界面）
void MainWindow::on_but_addfriends_clicked()
{
    if(m_addFriendsFlag == 0){
        m_dialogAdd = new AddFriends(this);
        connect(m_dialogAdd,&AddFriends::searchFriends,this,&MainWindow::sendMessageToServer);
        connect(m_dialogAdd,&AddFriends::addFriend,this,&MainWindow::goAddFriends);
        connect(this,&MainWindow::searchResult,m_dialogAdd,&AddFriends::searchResult);
        connect(m_dialogAdd, &AddFriends::customClose, this, [this]() {
            qDebug()<<"添加好友窗口重置了";
            m_addFriendsFlag = 0;
            m_dialogAdd->disconnect();
        });
        m_dialogAdd->show();
        m_addFriendsFlag = 1;
    }
    else if(m_addFriendsFlag == 1){
        m_dialogAdd->raise();
        if (m_dialogAdd) {
            int x = this->x() + (this->width() - m_dialogAdd->width()) / 2;
            int y = this->y() + (this->height() - m_dialogAdd->height()) / 2;
            m_dialogAdd->move(x, y);
        }
    }
}

// 点击添加好友按钮（联系人页）
void MainWindow::on_but_add0_clicked()
{
    if(m_addFriendsFlag == 0){
        m_dialogAdd = new AddFriends(this);
        connect(m_dialogAdd,&AddFriends::searchFriends,this,&MainWindow::sendMessageToServer);
        connect(m_dialogAdd,&AddFriends::addFriend,this,&MainWindow::goAddFriends);
        connect(this,&MainWindow::searchResult,m_dialogAdd,&AddFriends::searchResult);
        connect(m_dialogAdd, &AddFriends::customClose, this, [this]() {
            qDebug()<<"添加好友窗口重置了";
            m_addFriendsFlag = 0;
            m_dialogAdd->disconnect();
        });
        m_dialogAdd->show();
        m_addFriendsFlag = 1;
    }
    else if(m_addFriendsFlag == 1){
        m_dialogAdd->raise();
        if (m_dialogAdd) {
            int x = this->x() + (this->width() - m_dialogAdd->width()) / 2;
            int y = this->y() + (this->height() - m_dialogAdd->height()) / 2;
            m_dialogAdd->move(x, y);
        }
    }
}

// 搜索输入更新好友列表视图
void MainWindow::on_line_search2_textChanged(const QString &arg1)
{
    if(arg1 == "搜索")return;
    QRegularExpression regex(arg1, QRegularExpression::CaseInsensitiveOption);
    m_filterProxyModel->setFilterRegularExpression(regex);
    m_filterProxyModel->invalidate();
    ui->list_friends->viewport()->update();
}

// 搜索输入更新聊天列表视图
void MainWindow::on_line_search_textChanged(const QString &arg1)
{
    if(arg1 == "搜索")return;
    QRegularExpression regex(arg1, QRegularExpression::CaseInsensitiveOption);
    m_talkFilterProxyModel->setFilterRegularExpression(regex);
    m_talkFilterProxyModel->invalidate();
    ui->list_friends->viewport()->update();
}

// 发送信息
void MainWindow::sendMessage()
{
    if(ui->edit_input->toPlainText().isEmpty()) return;
    if(!SocketOnly::instance().isConnected()){
        return;
    }

    // 获取当前选中好友账号
    QString receiverAccount = ui->list_talks->currentIndex().data(Qt::UserRole + 1).toString();
    if(AccountMessageManager::getInstance()->getInfo(receiverAccount).account.isEmpty()){
        Dialog* dialog = new Dialog(this);
        dialog->transText("他不是您的好友!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    QJsonObject json;
    QString localTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 构造发送给服务器的JSON数据
    json["tag"] = "messages";
    json["messages"] = ui->edit_input->toPlainText();
    json["sender"] = m_myInfo.account;
    json["messagetype"] = "text";
    json["receiver"] = receiverAccount;
    json["timestamp"] = localTimeStr;
    json["uuid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 发送数据
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    // 清空输入框
    ui->edit_input->clear();

    QWidget *page = getPage(receiverAccount);
    if (!page) {
        return;
    }

    toAddMessageToDatabase(m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                         json["messages"].toString(), localTimeStr, localTimeStr, json["uuid"].toString());

    addMessageTo(page, m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                 json["messages"].toString(), localTimeStr, localTimeStr, json["uuid"].toString(), true);

    // 提升好友聊天列表位置
    liftSomebody(receiverAccount);
    registerPendingOutboundAckWatch(json["uuid"].toString());
}

// 发送图片
void MainWindow::on_but_tool_sendpic_clicked()
{
    // 发送图片消息逻辑
    if(!SocketOnly::instance().isConnected()){
        return;
    }

    // 获取当前选中的好友账号
    QString receiverAccount = ui->list_talks->currentIndex().data(Qt::UserRole + 1).toString();
    if(AccountMessageManager::getInstance()->getInfo(receiverAccount).account.isEmpty()){
        Dialog* dialog = new Dialog(this);
        dialog->transText("他不是您的好友!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    // 弹出文件对话框让用户选择图片
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("选择图片"), "", tr("Images (*.png *.xpm *.jpg *.jpeg *.bmp)"));
    // 检查用户是否选择了文件
    if (fileName.isEmpty()) {
        return; // 用户取消了选择
    }

    QPixmap pixmap(fileName);
    if (pixmap.isNull()) {
        Dialog* dialog = new Dialog(this);
        dialog->transText(tr("无法加载图片: %1").arg(fileName));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    QByteArray compressedImageData;
    QBuffer buffer(&compressedImageData);  // 关联字节数组的缓冲区
    const qint64 MAX_SIZE = 1 * 1024 * 1024;
    QSize targetSize = pixmap.size();

    if (targetSize.width() > 1920 || targetSize.height() > 1080) {
        targetSize.scale(1920, 1080, Qt::KeepAspectRatio);
        pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    int left = 10, right = 90;  // 质量范围：10~90
    int bestQuality = 10;       // 默认最低质量
    QByteArray tempData;        // 临时测试用字节数组
    QBuffer tempBuffer(&tempData);  // 临时缓冲区

    while (left <= right) {
        int mid = (left + right) / 2;  // 中间质量值
        tempData.clear();              // 清空临时数据
        tempBuffer.close();            // 关闭缓冲区
        if (!tempBuffer.open(QIODevice::WriteOnly)) {
            qWarning() << "MainWindow: tempBuffer open failed";
            break;
        }

        pixmap.save(&tempBuffer, "JPEG", mid);

        if (tempData.size() <= MAX_SIZE) {
            bestQuality = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    compressedImageData.clear();
    buffer.close();
    if (!buffer.open(QIODevice::WriteOnly)) {
        qWarning() << "MainWindow: buffer open failed";
        return;
    }
    pixmap.save(&buffer, "JPEG", bestQuality);

    if (compressedImageData.size() > MAX_SIZE) {
        targetSize = targetSize * 0.8;
        pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        compressedImageData.clear();
        buffer.close();
        if (!buffer.open(QIODevice::WriteOnly)) {
            qWarning() << "MainWindow: buffer open failed on retry";
            return;
        }
        pixmap.save(&buffer, "JPEG", bestQuality);
    }

    QString pixmapString = compressedImageData.toBase64();
    QString localTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 构造发送给服务器的JSON数据
    QJsonObject json;
    json["tag"] = "messages";
    json["messages"] = pixmapString;
    json["sender"] = m_myInfo.account;
    json["messagetype"] = "picture";
    json["receiver"] = receiverAccount;
    json["timestamp"] = localTimeStr;
    json["uuid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 发送数据
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    QWidget *page = getPage(receiverAccount);
    if (!page) {
        return;
    }

    toAddMessageToDatabase(m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                         pixmapString, localTimeStr, localTimeStr, json["uuid"].toString());

    addMessageTo(page, m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                 pixmapString, localTimeStr, localTimeStr, json["uuid"].toString(), true);

    // 提升好友聊天列表位置
    liftSomebody(receiverAccount);
    registerPendingOutboundAckWatch(json["uuid"].toString());
}

// 发送文件
void MainWindow::on_but_tool_sendfile_clicked()
{
    QString receiver = ui->list_talks->currentIndex().data(Qt::UserRole + 1).toString();
    if (AccountMessageManager::getInstance()->getInfo(receiver).account.isEmpty()) {
        Dialog* dialog = new Dialog(this);
        dialog->transText("他不是您的好友!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    if(!m_uploadFile["filename"].toString().isEmpty()){
        Dialog* dialog = new Dialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->transText("文件: "+ m_uploadFile["filename"].toString()+" 正在上传,请稍后再试!");
        dialog->show();
        return;
    }

    // 弹出文件对话框选择文件
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("选择要发送的文件"), QString(), tr("所有文件 (*)"));
    if (fileName.isEmpty()) {
        return; // 用户取消选择
    }

    // 创建文件，检查有效性
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        Dialog* dialog = new Dialog(this);
        dialog->transText("无法打开文件");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    // // 检查文件大小，如果太大则返回
    // qint64 fileSize = file.size();
    // const qint64 maxFileSize = 1LL * 1024 * 1024 * 1024 /2; // 0.5GB
    // if (fileSize > maxFileSize) {
    //     Dialog* dialog = new Dialog(this);
    //     dialog->transText("文件太大!");
    //     dialog->setAttribute(Qt::WA_DeleteOnClose);
    //     dialog->show();
    //     return;
    // }

    QFileInfo fileInfo(fileName);
    QString onlyFileName = fileInfo.fileName();
    m_uploadFile["filename"] = onlyFileName;

    setUpload(m_uploadFile["filename"].toString());

    QString localTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    file.close();

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_uploadFile["receiver"] = receiver;
    m_uploadFile["timestamp"] = localTimeStr;
    m_uploadFile["filepath"] = fileName;
    m_uploadFile["uuid"] = uuid;

    QMetaObject::invokeMethod(this, [=]() {
        sendDoc(QByteArray(), onlyFileName, localTimeStr, receiver, uuid);
    }, Qt::QueuedConnection);
}

// 开始录音
void MainWindow::on_but_tool_sendaudio_pressed()
{
    if (!m_audioBuffer) {
        qWarning() << "录音缓冲区未初始化";
        return;
    }
    if (m_audioSource) {
        // 如果正在录音，先停止
        if (m_audioSource->state() == QAudio::ActiveState) {
            m_audioSource->stop();
        }
        delete m_audioSource;
        m_audioSource = nullptr;
    }

    // 获取音频设备
    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qWarning() << "无可用录音设备";
        Dialog* dialog = new Dialog(this);
        dialog->transText("未检测到麦克风，无法录音");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    if (!inputDevice.isFormatSupported(m_format)) {
        qDebug() << "不支持的音频格式";
        return;
    }

    // 若缓冲区曾被播放占用，先关闭再复用
    if (m_audioBuffer->isOpen()) {
        m_audioBuffer->close();
    }
    m_audioBuffer->buffer().clear();
    m_audioBuffer->open(QIODevice::WriteOnly);

    // 创建音频源
    m_audioSource = new QAudioSource(inputDevice, m_format, this);

    // 开始捕获
    m_audioSource->start(m_audioBuffer);
    m_audioTimer->start(60000);

    qDebug() << "开始录音";
    setAudioBtnRecording(true);

    qDebug() << "录音中...";
}

// 停止录音并发送
void MainWindow::on_but_tool_sendaudio_released()
{
    m_audioTimer->stop();
    setAudioBtnRecording(false);

    if (!m_audioSource) {
        return;
    }
    if (m_audioSource->state() != QAudio::ActiveState) {
        return;
    }
    if (!SocketOnly::instance().isConnected()) {
        return;
    }

    QString receiverAccount = ui->list_talks->currentIndex().data(Qt::UserRole + 1).toString();
    if (AccountMessageManager::getInstance()->getInfo(receiverAccount).account.isEmpty()) {
        Dialog* dialog = new Dialog(this);
        dialog->transText("他不是您的好友!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    qDebug() << "停止录音";

    // 停止捕获
    m_audioSource->stop();

    // 获取录音数据
    m_audioBuffer->close();
    QByteArray audioData = m_audioBuffer->data();
    m_audioBuffer->buffer().clear();

    // 检查音频时长（太短则不发送）
    QString audioTime = getAudioTime(audioData);
    if (audioTime == "0s") {
        qDebug() << "录制时长太短,不发送";
        Dialog* dialog = new Dialog(this);
        dialog->transText("录制时长少于1秒,不发送!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    QString localTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 构造发送给服务器的JSON数据
    QJsonObject json;
    json["tag"] = "messages";
    json["messages"] = QString::fromLatin1(audioData.toBase64());
    json["sender"] = m_myInfo.account;
    json["messagetype"] = "audio";
    json["receiver"] = receiverAccount;
    json["timestamp"] = localTimeStr;
    json["uuid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 发送数据
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    QWidget *page = getPage(receiverAccount);
    if (!page) {
        return;
    }
    toAddMessageToDatabase(m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                         json["messages"].toString(), localTimeStr, localTimeStr, json["uuid"].toString());
    addMessageTo(page, m_myInfo.account, receiverAccount, json["messagetype"].toString(),
                 json["messages"].toString(), localTimeStr, localTimeStr, json["uuid"].toString(), true);

    liftSomebody(receiverAccount);
    registerPendingOutboundAckWatch(json["uuid"].toString());
}


// 向服务器发送搜索用户信息申请
void MainWindow::goSearchFriends(const QString &account)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonObject jsonObj;
    jsonObj["tag"] = "searchfriend";
    jsonObj["account"] = account;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    qDebug()<<"发送了搜索好友信息请求";
}

// 清理右边好友信息视图
void MainWindow::clearEditShow2()
{
    // 清理旧的布局和控件
    QLayout *existingLayout = ui->edit_show2->layout();
    if (existingLayout) {
        QLayoutItem *item;
        while ((item = existingLayout->takeAt(0)) != nullptr) {
            QWidget *widget = item->widget();
            if (widget) {
                widget->deleteLater();// 删除控件
            }
            delete item;// 删除布局项
        }
    }
    for (QWidget *widget : ui->edit_show2->findChildren<QWidget*>()) {
        if (qobject_cast<QPushButton*>(widget) || qobject_cast<QLabel*>(widget)) {
            // 检查是否是 QPushButton 或 QLabel
            widget->deleteLater();// 删除按钮或标签
        }
    }
}

// 更新右侧好友信息视图（普通好友）
void MainWindow::updateEditShow2Normal(const QModelIndex &index)
{
    clearEditShow2();// 清理旧控件
    // 从模型中获取好友的信息
    m_deleteFriendNum = ui->list_friends->currentIndex().data(Qt::UserRole+1).toString();
    QString accountNum = index.data(Qt::UserRole + 1).toString();
    QString nickname = AccountMessageManager::getInstance()->getInfo(accountNum).name;
    QString gender = AccountMessageManager::getInstance()->getInfo(accountNum).gender;
    QString signature = AccountMessageManager::getInstance()->getInfo(accountNum).signature;
    QPixmap pixmap = AvatarManager::getInstance()->loadAvator(accountNum, QSize(0,0));
    // 把右边的edit背景重绘
    ui->edit_show2->setBackground(pixmap);
    ui->edit_show2->repaint();
    // 添加间隔器
    QSpacerItem *spacertop = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_newLayout->addItem(spacertop);
    // 显示头像
    QLabel *iconLabel = new QLabel;
    int borderWidth = 4;
    int actualSize = 120 - 2 * borderWidth;

    QPixmap scaledPixmap = pixmap.scaled(
        actualSize, actualSize,
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation
        );
    QPixmap roundedIconPixmap = AvatarManager::getRoundedPixmap(scaledPixmap, 6);
    iconLabel->setPixmap(roundedIconPixmap);
    iconLabel->setFixedSize(120, 120);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(
        QString("QLabel {"
                "    border: %1px solid rgba(255, 153, 179, 1);"
                "    border-radius: 10px;"
                "    background-color: white;"
                "}").arg(borderWidth));

    m_newLayout->addWidget(iconLabel, 0, Qt::AlignCenter);
    // 添加信息标签
    auto addTextEditWithStyle = [&](const QString &prefix, const QString &text) {
        AutoClearTextEdit *textEdit = new AutoClearTextEdit(prefix + text);
        textEdit->setContextMenuPolicy(Qt::NoContextMenu);
        textEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        textEdit->setFixedSize(300, 50);
        textEdit->setStyleSheet(
            "font-size: 16px; "
            "font-weight: bold; "
            "color: #333333; "
            "padding: 5px; "
            "background: rgba(255, 255, 255, 0); "
            "border: 2px solid black; "
            "border-radius: 5px;");
        textEdit->setAlignment(Qt::AlignCenter);
        textEdit->setReadOnly(true);// 设置为只读
        textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);// 禁用垂直滚动条
        textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);// 禁用水平滚动条
        return textEdit;
    };
    // 添加间隔器
    QSpacerItem *spacer0 = new QSpacerItem(0, 15, QSizePolicy::Fixed, QSizePolicy::Minimum);
    m_newLayout->addItem(spacer0);
    // 添加信息
    if (!nickname.isEmpty()) m_newLayout->addWidget(addTextEditWithStyle("昵称: ", nickname));
    if (!accountNum.isEmpty()) m_newLayout->addWidget(addTextEditWithStyle("账号: ", accountNum));
    if (!gender.isEmpty()) m_newLayout->addWidget(addTextEditWithStyle("性别: ", gender));
    if (!signature.isEmpty()) m_newLayout->addWidget(addTextEditWithStyle("签名: ", signature));
    // 添加间隔器
    QSpacerItem *spacer1 = new QSpacerItem(0, 5, QSizePolicy::Fixed, QSizePolicy::Minimum);
    m_newLayout->addItem(spacer1);
    // 创建按钮布局
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setAlignment(Qt::AlignCenter);// 按钮布局居中
    // 添加发送信息按钮
    QPushButton *sendButton = new QPushButton("发送信息");
    sendButton->setCursor(Qt::PointingHandCursor);
    sendButton->setFixedSize(140, 55);
    sendButton->setStyleSheet(
        "QPushButton {"
        "    font: 12pt 'Microsoft YaHei UI';"
        "    background-color: rgb(5, 186, 251);"
        "    color: white;"
        "    font-weight: bold; "
        "    border-radius: 15px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(5, 186, 251, 0.7);"
        "    font-weight: bold; "
        "    color: white;"
        "    border-radius: 15px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(0, 123, 255, 0.8);"
        "    font-weight: bold; "
        "    color: rgba(255, 255, 255, 0.9);"
        "    border-radius: 15px;"
        "}"
        );
    buttonLayout->addWidget(sendButton);
    // 添加间隔器
    QSpacerItem *spacer = new QSpacerItem(25, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    buttonLayout->addItem(spacer);
    // 添加删除好友按钮
    QPushButton *deleteButton = new QPushButton("删除好友");
    deleteButton->setCursor(Qt::PointingHandCursor);
    deleteButton->setFixedSize(140, 55);
    deleteButton->setStyleSheet(
        "QPushButton {"
        "    font: 12pt 'Microsoft YaHei UI';"
        "    background-color: rgb(255, 0, 0);"
        "    color: white;"
        "    font-weight: bold; "
        "    border-radius: 15px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 0, 0, 0.7);"
        "    font-weight: bold; "
        "    color: white;"
        "    border-radius: 15px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(200, 0, 0, 0.8);"
        "    font-weight: bold; "
        "    color: rgba(255, 255, 255, 0.9);"
        "    border-radius: 15px;"
        "}"
        );
    buttonLayout->addWidget(deleteButton);
    // 将按钮布局添加到主布局中
    m_newLayout->addLayout(buttonLayout);
    // 添加间隔器
    QSpacerItem *spacer2 = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_newLayout->addItem(spacer2);
    // 增加间距
    m_newLayout->setSpacing(3);
    m_newLayout->setContentsMargins(5, 5, 5, 5);
    // 为按钮添加点击事件
    connect(sendButton, &QPushButton::clicked, this, [=]() {
        qDebug() << "发送信息按钮被点击";
        sendMessageToFriend(accountNum);
    });
    connect(deleteButton, &QPushButton::clicked, this, [&]() {
        qDebug() << "删除好友按钮被点击";
        if(AccountMessageManager::getInstance()->getInfo(m_deleteFriendNum).account.isEmpty()){
            Dialog* dialog = new Dialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->transText("他不在您的好友列表当中!");
            dialog->show();
            return;
        }

        ChoiceDialog *dialog = new ChoiceDialog(this);
        dialog->transText("你确定要删除好友吗?");
        dialog->transButText("确认", "取消");
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        // 连接按钮信号
        connect(dialog, &ChoiceDialog::accepted, this, &MainWindow::deleteFriend);

        dialog->open();

    });
    // 设置布局
    ui->edit_show2->setLayout(m_newLayout);
}

// 更新右侧好友信息视图（好友申请列表）
void MainWindow::updateEditShow2New()
{
    if(ui->list_friends->currentIndex().row() == 1){
        QModelIndex secondItemIndex = ui->list_friends->model()->index(1, 0);
        ui->list_friends->model()->setData(secondItemIndex, false, Qt::UserRole + 20);
        ui->list_friends->model()->dataChanged(secondItemIndex, secondItemIndex, {Qt::UserRole + 20});
    }
    clearEditShow2();// 清理旧控件
    // 把右边的edit背景重绘为默认
    ui->edit_show2->setDefaultBack();
    ui->edit_show2->repaint();
    if(m_newFriendArray.empty()){
        QLabel *label = new QLabel(this);
        label->setText("目前没有好友申请哦。");
        label->setStyleSheet("font-size: 22px; color: #333333; font-weight: bold;");
        m_newLayout->addWidget(label, Qt::AlignCenter);
    }
    for (const auto &friendRequest : m_newFriendArray) {// 遍历添加好友申请用户
        QString friendNickname = friendRequest.name;
        QString friendavatorBase64 = friendRequest.avator_base64;
        QByteArray avatarByteArray = QByteArray::fromBase64(friendavatorBase64.toUtf8());
        QImage avatarImage;
        avatarImage.loadFromData(avatarByteArray);
        QPixmap avatarPixmap = QPixmap::fromImage(avatarImage);
        avatarPixmap = AvatarManager::getRoundedPixmap(avatarPixmap, 5);
        QHBoxLayout *requestLayout = new QHBoxLayout();
        requestLayout->setContentsMargins(0, 0, 0, 0);
        requestLayout->setSpacing(10);
        requestLayout->setSizeConstraint(QLayout::SetMinimumSize);
        // 添加头像
        LabelFriendAva *avatorLabel = new LabelFriendAva(this, false);
        avatorLabel->setPixmap(avatarPixmap.scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        avatorLabel->setFixedSize(60, 60);
        avatorLabel->setScaledContents(true);
        avatorLabel->setCursor(Qt::PointingHandCursor);
        requestLayout->addWidget(avatorLabel);
        requestLayout->setAlignment(avatorLabel, Qt::AlignLeft);
        // 给头像添加申请人信息
        avatorLabel->setProperty("account",QVariant(friendRequest.account));
        // 添加名称
        QLabel *nicknameLabel = new QLabel(friendNickname);
        nicknameLabel->setStyleSheet("font-size: 16px; color: #333333;");
        requestLayout->addWidget(nicknameLabel);
        requestLayout->setAlignment(nicknameLabel, Qt::AlignLeft);
        QSpacerItem *spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        requestLayout->addItem(spacer);
        // 接受按钮
        QPushButton *acceptButton = new QPushButton("接受");
        acceptButton->setCursor(Qt::PointingHandCursor);
        // 存储数据到按钮
        acceptButton->setProperty("sender", friendRequest.account);
        acceptButton->setStyleSheet(
            "QPushButton {"
            "    font: 14pt 'Microsoft YaHei UI';"
            "    background-color: rgb(5, 186, 251);"
            "    color: white;"
            "    border-radius: 5px;"
            "    min-width: 80px;"
            "    min-height: 30px;"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(5, 186, 251, 0.7);"
            "}"
            "QPushButton:pressed {"
            "    background-color: rgba(255, 0, 0, 0.5);"
            "}"
            );
        connect(acceptButton, &QPushButton::clicked, this, [this, acceptButton]() {
            // 创建非阻塞确认对话框
            ChoiceDialog *dialog = new ChoiceDialog(this);
            dialog->transText("你确定要接受好友申请吗？");
            dialog->transButText("确定", "取消");
            dialog->setAttribute(Qt::WA_DeleteOnClose);  // 自动删除

            // 获取发送者ID
            QString senderId = acceptButton->property("sender").toString();

            // 连接确认信号
            connect(dialog, &ChoiceDialog::accepted, this, [this, senderId]() {
                if(AccountMessageManager::getInstance()->getSize() >= MAX_FRIENDS){
                    QString text =  "您的好友数已达上限！!";
                    Dialog* dialog = new Dialog(this);
                    dialog->transText(text);
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->show();
                    return;
                }
                if (!senderId.isEmpty()) {
                    acceptAddFriends(m_myInfo.account, senderId);
                }
            });
            dialog->show();
        });
        requestLayout->addWidget(acceptButton);
        requestLayout->setAlignment(acceptButton, Qt::AlignRight);
        // 拒绝按钮
        QPushButton *rejectButton = new QPushButton("拒绝");
        rejectButton->setCursor(Qt::PointingHandCursor);
        // 存储数据到按钮
        rejectButton->setProperty("sender", friendRequest.account);
        rejectButton->setStyleSheet(
            "QPushButton {"
            "    font: 14pt 'Microsoft YaHei UI';"
            "    background-color: rgb(255, 0, 0);"
            "    color: white;"
            "    border-radius: 5px;"
            "    min-width: 80px;"
            "    min-height: 30px;"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(255, 0, 0, 0.7);"
            "}"
            "QPushButton:pressed {"
            "    background-color: rgba(255, 0, 0, 0.5);"
            "}"
            );
        connect(rejectButton, &QPushButton::clicked, this, [this, rejectButton]() {
            ChoiceDialog *dialog = new ChoiceDialog(this);
            dialog->transText("你确定要拒绝好友申请吗？");
            dialog->transButText("确定", "取消");
            dialog->setAttribute(Qt::WA_DeleteOnClose);

            QString senderId = rejectButton->property("sender").toString();

            connect(dialog, &ChoiceDialog::accepted, this, [this, senderId]() {
                if (!senderId.isEmpty()) {
                    rejectAddFriends(m_myInfo.account, senderId);
                }
            });

            dialog->show();
        });
        requestLayout->addWidget(rejectButton);
        requestLayout->setAlignment(rejectButton, Qt::AlignRight);
        m_newLayout->addLayout(requestLayout, Qt::AlignTop);
    }
    // 最下方添加一个伸缩器
    QSpacerItem *bottomSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_newLayout->addItem(bottomSpacer);
}

// 删除好友
void MainWindow::deleteFriend()
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonObject jsonObj;
    jsonObj["tag"] = "deletefriend";
    jsonObj["account"] = m_myInfo.account;
    jsonObj["friend"] = m_deleteFriendNum;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    qDebug()<<"发送了删除好友请求";
}

// 添加好友
void MainWindow::goAddFriends(const QString &friendAccount)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    // 查看是否是本人的账号
    if(friendAccount == m_myInfo.account){
        // 弹窗
        QString text =  "你不能添加自己为好友!";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setModal(false);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    // 检查是否已经添加好友了
    if(!AccountMessageManager::getInstance()->getInfo(friendAccount).account.isEmpty()){
        // 弹窗
        QString text =  "对方已经是你的好友了!";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setModal(false);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    if(AccountMessageManager::getInstance()->getSize() >= MAX_FRIENDS){
        QString text =  "您的好友数已达上限！!";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    QJsonObject jsonObj;
    jsonObj["tag"] = "addfriend";
    jsonObj["account"] = m_myInfo.account;// 谁要加（当前用户）
    jsonObj["friend"] = friendAccount;// 要加谁
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    qDebug()<<"发送了添加好友请求";
}

// 弹出修改个人资料窗口
void MainWindow::changeInfo()
{
    if (m_changeInfoFlag == 0) {
        AccountInfo tmpInfo;
        tmpInfo.account = m_myInfo.account;
        tmpInfo.name = m_myInfo.name;
        tmpInfo.gender = m_myInfo.gender;
        tmpInfo.signature = m_myInfo.signature;
        m_dialogChangeInfo = new ChangeInformation(tmpInfo, this);
        connect(m_dialogChangeInfo, &ChangeInformation::sendMessage, this, &MainWindow::sendMessageToServer);
        connect(this, &MainWindow::changeResult, m_dialogChangeInfo, &ChangeInformation::dealResult);
        connect(m_dialogChangeInfo, &ChangeInformation::customClose, this, [this]() {
            qDebug() << "修改信息窗口重置了";
            m_changeInfoFlag = 0;
            m_dialogChangeInfo->disconnect();
        });
        m_dialogChangeInfo->show();
        m_changeInfoFlag = 1;
    } else if (m_changeInfoFlag == 1) {
        m_dialogChangeInfo->raise();
        if (m_dialogChangeInfo) {
            int x = this->x() + (this->width() - m_dialogChangeInfo->width()) / 2;
            int y = this->y() + (this->height() - m_dialogChangeInfo->height()) / 2;
            m_dialogChangeInfo->move(x, y);
        }
    }
}

// 聊天列表菜单选择完毕
void MainWindow::listtalkChoice(const QJsonObject& json)
{
    if (json.empty()) {
        const QModelIndex idx = ui->list_talks->currentIndex();
        if (idx.isValid()) {
            ui->list_talks->clicked(idx);
        }
        return;
    }
    else if(json["tag"] == "sendmessage"){// 发送消息
        ui->edit_input->setFocus();
        QCursor::setPos(ui->edit_input->mapToGlobal(QPoint(30, 30)));// 移动鼠标到edit_input内
    }
    else if(json["tag"] == "message"){// 好友信息
        const QModelIndex talkIdx = ui->list_talks->currentIndex();
        if (!talkIdx.isValid()) {
            return;
        }
        QString account = talkIdx.data(Qt::UserRole + 1).toString();
        FriendMessage *dialog = new FriendMessage(account,this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
    else if(json["tag"] == "deletefriend"){// 删除好友
        const QModelIndex talkIdx = ui->list_talks->currentIndex();
        if (!talkIdx.isValid()) {
            return;
        }
        if(AccountMessageManager::getInstance()->getInfo(talkIdx.data(Qt::UserRole + 1).toString())
                .account.isEmpty()){
            Dialog* dialog = new Dialog(this);
            dialog->transText("他不在您的好友列表当中!");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            return;
        }
        ChoiceDialog *dialog = new ChoiceDialog(this);
        dialog->transText("你确定要删除好友吗?");
        dialog->transButText("确认","取消");
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        const QString deleteFriendAccount = talkIdx.data(Qt::UserRole + 1).toString();
        connect(dialog, &ChoiceDialog::accepted, this, [this, deleteFriendAccount]() {
            if(!SocketOnly::instance().isConnected()){
                return;
            }
            QJsonObject jsonObj;
            jsonObj["tag"] = "deletefriend";
            jsonObj["account"] = m_myInfo.account;
            jsonObj["friend"] = deleteFriendAccount;
            QJsonDocument jsonDoc(jsonObj);
            QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
            SocketOnly::instance().sendData(jsonData);

            qDebug()<<"发送了删除好友请求"<<jsonObj["friend"];
        });

        dialog->open();
    }
    else if(json["tag"] == "deletetalk"){// 从消息列表移除
        const QModelIndex talkIdx = ui->list_talks->currentIndex();
        if (!talkIdx.isValid()) {
            return;
        }
        deleteSomeoneInTalkList(talkIdx.data(Qt::UserRole + 1).toString());
    }
    else if(json["tag"] == "clearmessage"){// 清空聊天记录
        const QModelIndex clearIdx = ui->list_talks->currentIndex();
        if (!clearIdx.isValid()) {
            return;
        }
        const QString clearAccountNum = clearIdx.data(Qt::UserRole + 1).toString();

        ChoiceDialog *dialog = new ChoiceDialog(this);
        dialog->transText("你确定要清空聊天记录吗？");
        dialog->transButText("确定", "取消");
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        connect(dialog, &ChoiceDialog::accepted, this, [this, clearAccountNum]() {
            QString accountNum = clearAccountNum;
            QSqlQuery qry(m_db);
            qry.prepare("DELETE FROM messages WHERE sender = :friend OR receiver = :friend");
            qry.bindValue(":friend", accountNum);
            qry.exec();

            if (QStandardItem *talkItem = m_talkListItems.value(accountNum, nullptr)) {
                talkItem->setData(QString(), Qt::UserRole + 5);
                talkItem->setData(QString(), Qt::UserRole + 6);
                talkItem->setData(0, Qt::UserRole + 10);
                const QModelIndex src = m_talkModel->indexFromItem(talkItem);
                if (src.isValid()) {
                    const QModelIndex proxyIdx = m_talkFilterProxyModel->mapFromSource(src);
                    if (proxyIdx.isValid()) {
                        m_talkFilterProxyModel->dataChanged(proxyIdx, proxyIdx, {Qt::UserRole + 10});
                    }
                }
            }

            QWidget *page = getPage(accountNum);
            if (page) {
                QListWidget *list = page->findChild<QListWidget *>();
                if (list) {
                    list->clear();
                    list->setObjectName("notime");
                }
            }

            QVariantMap emptyTalkData;
            emptyTalkData["unread"] = 0;          // 未读数重置为0
            emptyTalkData["latest_msg"] = " ";     // 最新消息清空
            emptyTalkData["timestamp"] = " ";      // 时间戳清空
            m_talkCache[accountNum] = emptyTalkData;
            clearSomebodyTalkMessages(accountNum);

            Dialog* successDialog = new Dialog(this);
            successDialog->setAttribute(Qt::WA_DeleteOnClose);
            successDialog->transText("清空聊天记录成功!");
            successDialog->show();
        });

        dialog->open();
    }
    else if (json["tag"] == "aianalyze") {
        if (m_aiAnalysisInProgress) {
            Dialog *busy = new Dialog(this);
            busy->setAttribute(Qt::WA_DeleteOnClose);
            busy->transText(QStringLiteral("已有分析在进行，请等待结束后再试"));
            busy->show();
            return;
        }
        const QModelIndex talkIdx = ui->list_talks->currentIndex();
        if (!talkIdx.isValid()) {
            return;
        }
        const QString peerAccount = talkIdx.data(Qt::UserRole + 1).toString();
        if (peerAccount.isEmpty()) {
            return;
        }
        AiAnalyzeDialog *dlg = new AiAnalyzeDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowModality(Qt::NonModal);
        const AccountInfo &peerInfo = AccountMessageManager::getInstance()->getInfo(peerAccount);
        const QString peerName = peerInfo.name.trimmed().isEmpty() ? peerAccount : peerInfo.name.trimmed();
        dlg->setPeerDisplayName(peerName);

        connect(dlg, &QDialog::accepted, this, [this, dlg, peerAccount]() {
            const bool absMode = dlg->isAbsoluteTimeMode();
            int rv = 1;
            QString unit;
            if (!absMode) {
                rv = dlg->rangeValue();
                if (rv < 1 || rv > 999) {
                    Dialog *bad = new Dialog(this);
                    bad->setAttribute(Qt::WA_DeleteOnClose);
                    bad->transText(QStringLiteral("时间范围请输入 1～999 的整数"));
                    bad->show();
                    return;
                }
                unit = dlg->rangeUnitKey();
                if (unit.isEmpty()) {
                    Dialog *bad = new Dialog(this);
                    bad->setAttribute(Qt::WA_DeleteOnClose);
                    bad->transText(QStringLiteral("请选择时间单位"));
                    bad->show();
                    return;
                }
            } else {
                // 绝对时间点：起点/终点选择，终点默认当前时间
                const QDateTime st = dlg->absoluteStartDateTime();
                const QDateTime en = dlg->absoluteEndDateTime();
                if (!st.isValid() || !en.isValid() || st > en) {
                    Dialog *bad = new Dialog(this);
                    bad->setAttribute(Qt::WA_DeleteOnClose);
                    bad->transText(QStringLiteral("请选择有效的开始/结束时间点（开始需 <= 结束）"));
                    bad->show();
                    return;
                }
            }
            QString prompt = dlg->promptText();
            if (prompt.isEmpty()) {
                prompt = QStringLiteral("汇总这段时间对话内容");
            }
            if (!SocketOnly::instance().isConnected()) {
                return;
            }
            if (m_aiSessionToken.isEmpty()) {
                Dialog *tip = new Dialog(this);
                tip->setAttribute(Qt::WA_DeleteOnClose);
                tip->transText(QStringLiteral("会话未就绪，请重新登录后再试"));
                tip->show();
                return;
            }
            if (m_aiWaitDialog) {
                m_aiWaitDialog->close();
            }
            m_aiWaitDialog = new Dialog(this);
            m_aiWaitDialog->setAttribute(Qt::WA_DeleteOnClose);
            m_aiWaitDialog->transText(QStringLiteral("分析可能耗费一定时间,请稍等…"));
            m_aiWaitDialog->show();
            m_aiAnalysisInProgress = true;

            QJsonObject req;
            req["tag"] = QStringLiteral("chat_ai_analyze");
            req["account"] = m_myInfo.account;
            req["peer"] = peerAccount;
            if (absMode) {
                const QDateTime st = dlg->absoluteStartDateTime();
                const QDateTime en = dlg->absoluteEndDateTime();
                req["range_start_dt"] = st.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
                req["range_end_dt"] = en.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
                // 兼容字段：服务端若未识别绝对字段，可兜底为一个合法范围
                req["range_value"] = 1;
                req["range_unit"] = QStringLiteral("day");
            } else {
                req["range_value"] = rv;
                req["range_unit"] = unit;
            }
            req["prompt"] = prompt;
            req["session_token"] = m_aiSessionToken;
            req["request_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QJsonDocument doc(req);
            SocketOnly::instance().sendData(doc.toJson(QJsonDocument::Compact));
        });
        dlg->show();
    }
}

// 发送注销申请
void MainWindow::goLogout(const QJsonObject &json)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 拒绝好友申请
void MainWindow::rejectAddFriends(const QString &account, const QString &sender)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonObject json;
    json["tag"] = "newfriends";
    json["answer"] = "reject";
    json["account"] = account;
    json["sender"] = sender;
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    for (int i = m_newFriendArray.size() - 1; i >= 0; --i) {
        if (m_newFriendArray[i].account == json["sender"].toString()) {
            m_newFriendArray.removeAt(i);
        }
    }
    updateEditShow2New();
}

// 接受好友申请
void MainWindow::acceptAddFriends(const QString &account, const QString &sender)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonObject json;
    json["tag"] = "newfriends";
    json["answer"] = "accept";
    json["account"] = account;
    json["sender"] = sender;
    QJsonDocument jsonDoc(json);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 播放录音
void MainWindow::startAudio(const QString &audioPath)
{
    // 校验路径有效性
    if (audioPath.isEmpty()) {
        qDebug() << "音频文件路径为空，无法播放";
        return;
    }
    if (!QFile::exists(audioPath)) {
        qWarning() << "音频文件不存在：" << audioPath;
        return;
    }

    // 读取音频文件数据到内存
    QFile audioFile(audioPath);
    if (!audioFile.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开音频文件：" << audioPath << " 错误：" << audioFile.errorString();
        return;
    }
    QByteArray audioData = audioFile.readAll();
    audioFile.close();

    // 校验读取到的音频数据
    if (audioData.isEmpty()) {
        qDebug() << "音频文件读取为空或文件大小为0：" << audioPath;
        return;
    }

    // 确保播放设备已初始化（m_audioSink 可能未创建）
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        qWarning() << "无可用播放设备";
        return;
    }
    if (!QMediaDevices::defaultAudioOutput().isFormatSupported(m_format)) {
        qWarning() << "音频格式不支持，尝试使用默认格式";
        m_format = outputDevice.preferredFormat();
    }
    if (!m_audioSink) {
        m_audioSink = new QAudioSink(outputDevice, m_format, this);
    }

    // 计算音频数据的MD5哈希值
    QByteArray currentHash = QCryptographicHash::hash(audioData, QCryptographicHash::Md5);

    if (m_audioSink->state() == QAudio::ActiveState) {
        stopAudio(); // 停止当前播放

        if (currentHash == m_playingAudioHash) {
            qDebug() << "正在播放相同的音频（路径：" << audioPath << "），停止播放";
            return;
        }
    }
    m_playingAudioHash = currentHash;

    // 重置音频缓冲区并写入读取到的音频数据
    if (m_audioBuffer) {
        delete m_audioBuffer;
        m_audioBuffer = nullptr;
    }
    m_audioBuffer = new QBuffer(this);
    m_audioBuffer->setData(audioData);

    if (!m_audioBuffer->open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开音频缓冲区";
        delete m_audioBuffer;
        m_audioBuffer = nullptr;
        return;
    }

    // 启动音频播放
    m_audioBuffer->seek(0);
    m_audioSink->start(m_audioBuffer);

    qDebug() << "开始播放音频（路径：" << audioPath << "），数据大小:" << audioData.size() << "字节";
}

// 停止播放录音
void MainWindow::stopAudio()
{
    if (m_audioSink && m_audioSink->state() != QAudio::StoppedState) {
        m_audioSink->stop();
    }

    if (m_audioTimer && m_audioTimer->isActive()) {
        m_audioTimer->stop();
    }

    if (m_audioBuffer && m_audioBuffer->isOpen()) {
        m_audioBuffer->close();
    }

    qDebug() << "停止音频播放";
}

// 发送文件
void MainWindow::sendDoc(const QByteArray &jsonData, const QString &filename,
                         const QString &timestamp, const QString &receiver, const QString &uuid)
{
    Q_UNUSED(jsonData);
    m_uploadTransferStartMs = QDateTime::currentMSecsSinceEpoch();

    const QString filePath = m_uploadFile.value("filepath").toString();
    const QString sender = m_myInfo.account;
    SocketDocWrite &docWriter = SocketDocWrite::instance();

    // 防止重复连接导致多次回调（重复弹窗/重复更新）
    disconnect(&docWriter, &SocketDocWrite::uploadProgress, this, nullptr);
    disconnect(&docWriter, &SocketDocWrite::fileUpdate, this, nullptr);
    disconnect(&docWriter, &SocketDocWrite::socketError, this, nullptr);

    connect(&docWriter, &SocketDocWrite::uploadProgress,
            this, [this](const QString &u, qint64 receivedBytes, qint64 totalBytes) {
                if (m_uploadFile.value("uuid").toString() != u) return;
                if (totalBytes <= 0) return;
                const int percent = qBound(0, static_cast<int>((receivedBytes * 100) / totalBytes), 99);
                m_lastUploadProgressPercent = percent;
                setButtonProgress(ui->but_upload, percent,
                                  QStringLiteral("正在上传文件: %1")
                                      .arg(m_uploadFile.value("filename").toString()));
            }, Qt::QueuedConnection);

    connect(&docWriter, &SocketDocWrite::socketError,
            this, [this, uuid](const QString &err) {
                if (m_uploadFile.value("uuid").toString() != uuid) return;
                Dialog* dialog = new Dialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->transText(err.isEmpty() ? "上传失败，请重试" : err);
                dialog->show();
                m_uploadFile = QJsonObject();
                clearUpload();
            }, Qt::QueuedConnection);

    docWriter.sendFile(filePath, sender, receiver, filename, timestamp, uuid);
    // 连接文件发送socket的信号槽
    connect(&docWriter, &SocketDocWrite::fileUpdate,
            this,[=](const QString& fileName, const QString& uuid){
                if (m_uploadFile.value("uuid").toString() != uuid) return;
                pushMessageToDatabase(uuid);
                Dialog* dialog = new Dialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->transText("文件: "+ fileName+" 上传完毕");
                dialog->show();
                m_uploadFile = QJsonObject();
                clearUpload();
            }, Qt::QueuedConnection);

    QWidget *page = getPage(receiver);

    if (page) {
        toAddMessageToDatabase(m_myInfo.account,receiver, "document", filename, timestamp, timestamp, uuid);
        m_uploadFile["record_account"] = m_myInfo.account;
        m_uploadFile["record_receiver"] = receiver;
        m_uploadFile["record_timestamp"] = timestamp;
        m_uploadFile["record_filename"] = filename;

        addMessageTo(page, m_myInfo.account, receiver, "document", filename, timestamp, timestamp, uuid, true);
        registerPendingOutboundAckWatch(uuid);
    }
    liftSomebody(receiver);
}

// 打开网盘窗口
void MainWindow::openCloudDrive()
{
    if (!m_cloudDrive) {
        m_cloudDrive = new CloudDrive(this);
        connect(m_cloudDrive, &CloudDrive::cloudUploadRequested, this, &MainWindow::startCloudUpload);
        connect(m_cloudDrive, &CloudDrive::cloudSearchRequested, this, &MainWindow::searchCloudFile);
        connect(m_cloudDrive, &CloudDrive::cloudDownloadRequested, this, &MainWindow::downloadCloudFile);
        connect(m_cloudDrive, &CloudDrive::cloudListMyFilesRequested, this, &MainWindow::listMyCloudFiles);
    }

    if (!m_cloudDrive->isVisible()) {
        const int x = this->x() + (this->width() - m_cloudDrive->width()) / 2;
        const int y = this->y() + (this->height() - m_cloudDrive->height()) / 2;
        m_cloudDrive->move(x, y);
    }
    m_cloudDrive->show();
    m_cloudDrive->raise();
    m_cloudDrive->refreshMyFilesList();
}

// 网盘上传
void MainWindow::startCloudUpload()
{
    if (!m_uploadFile["filename"].toString().isEmpty()) {
        Dialog *dialog = new Dialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->transText(QStringLiteral("文件: %1 正在上传，请稍后再试！").arg(m_uploadFile["filename"].toString()));
        dialog->show();
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(nullptr, tr("选择要上传到网盘的文件"), QString(), tr("所有文件 (*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        Dialog *dialog = new Dialog(this);
        dialog->transText(QStringLiteral("无法打开文件"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }

    // const qint64 fileSize = file.size();
    // const qint64 maxFileSize = 1LL * 1024 * 1024 * 1024 / 2;
    // if (fileSize > maxFileSize) {
    //     Dialog *dialog = new Dialog(this);
    //     dialog->transText(QStringLiteral("文件太大！"));
    //     dialog->setAttribute(Qt::WA_DeleteOnClose);
    //     dialog->show();
    //     return;
    // }

    const QFileInfo fileInfo(fileName);
    const QString onlyFileName = fileInfo.fileName();
    const QString localTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    file.close();

    m_uploadFile = QJsonObject();
    m_uploadFile["filename"] = onlyFileName;
    m_uploadFile["filepath"] = fileName;
    m_uploadFile["uuid"] = uuid;
    m_uploadFile["cloud"] = true;

    if (m_cloudDrive) {
        m_cloudDrive->setUploadInProgress(true);
    }

    setUpload(onlyFileName);

    sendCloudDoc(fileName, onlyFileName, localTimeStr, uuid);
}

// 经文档通道上传网盘文件：绑定进度/错误/cloudFileUploaded，完成后刷新我的列表
void MainWindow::sendCloudDoc(const QString &filepath, const QString &filename,
                              const QString &timestamp, const QString &uuid)
{
    Q_UNUSED(filepath);
    m_uploadTransferStartMs = QDateTime::currentMSecsSinceEpoch();
    SocketDocWrite &docWriter = SocketDocWrite::instance();

    disconnect(&docWriter, &SocketDocWrite::uploadProgress, this, nullptr);
    disconnect(&docWriter, &SocketDocWrite::cloudFileUploaded, this, nullptr);
    disconnect(&docWriter, &SocketDocWrite::fileUpdate, this, nullptr);
    disconnect(&docWriter, &SocketDocWrite::socketError, this, nullptr);

    connect(&docWriter, &SocketDocWrite::uploadProgress,
            this, [this](const QString &u, qint64 receivedBytes, qint64 totalBytes) {
                if (m_uploadFile.value("uuid").toString() != u) return;
                if (totalBytes <= 0) return;
                const int percent = qBound(0, static_cast<int>((receivedBytes * 100) / totalBytes), 99);
                m_lastUploadProgressPercent = percent;
                setButtonProgress(ui->but_upload, percent,
                                  QStringLiteral("正在上传网盘文件: %1")
                                      .arg(m_uploadFile.value("filename").toString()));
                const qint64 elapsedMs = m_uploadTransferStartMs > 0
                                             ? QDateTime::currentMSecsSinceEpoch() - m_uploadTransferStartMs
                                             : 0;
                const QString speedLine = formatTransferSpeedMbps(receivedBytes, elapsedMs);
                if (m_cloudDrive) {
                    m_cloudDrive->setUploadProgress(percent, speedLine,
                                                    m_uploadFile.value("filename").toString());
                }
            }, Qt::QueuedConnection);

    connect(&docWriter, &SocketDocWrite::socketError,
            this, [this, uuid](const QString &err) {
                if (m_uploadFile.value("uuid").toString() != uuid) return;
                Dialog *dialog = new Dialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->transText(err.isEmpty() ? QStringLiteral("网盘上传失败，请重试") : err);
                dialog->show();
                if (m_cloudDrive) {
                    m_cloudDrive->setUploadInProgress(false);
                }
                m_uploadFile = QJsonObject();
                clearUpload();
            }, Qt::QueuedConnection);

    connect(&docWriter, &SocketDocWrite::cloudFileUploaded,
            this, [this, uuid](const QString &fileId, const QString &name, qint64) {
                if (m_uploadFile.value("uuid").toString() != uuid && fileId != uuid) return;
                disconnect(&SocketDocWrite::instance(), &SocketDocWrite::uploadProgress, this, nullptr);
                if (m_cloudDrive) {
                    m_cloudDrive->showUploadedFileId(fileId, name);
                    m_cloudDrive->setUploadInProgress(false);
                }
                setButtonProgress(ui->but_upload, 100,
                                  QStringLiteral("网盘上传完成: %1").arg(name));
                m_uploadFile = QJsonObject();
                clearUpload();
                listMyCloudFiles();
            }, Qt::BlockingQueuedConnection);

    docWriter.sendCloudFile(m_uploadFile.value("filepath").toString(),
                            m_myInfo.account,
                            filename,
                            timestamp,
                            uuid);
}

// 查询网盘文件
void MainWindow::searchCloudFile(const QString &fileId)
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "searchcloudfile";
    jsonObj["file_id"] = fileId;
    jsonObj["account"] = m_myInfo.account;
    sendMessageToServer(jsonObj);
}

// 下载网盘文件
void MainWindow::downloadCloudFile(const QString &fileId, const QString &suggestedFilename)
{
    if (!m_savePath.isEmpty() || m_downloadingFile.isOpen()) {
        Dialog *dialog = new Dialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->transText(QStringLiteral("已有文件正在下载，请稍后再试"));
        dialog->show();
        return;
    }

    QString defaultPath;
    if (!suggestedFilename.isEmpty()) {
        defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                          .filePath(suggestedFilename);
    }

    const QString savePath = QFileDialog::getSaveFileName(
        nullptr, tr("保存网盘文件"), defaultPath, tr("所有文件 (*)"));
    if (savePath.isEmpty()) {
        return;
    }

    m_savePath = savePath;
    setDownLoad(savePath);

    const QString displayName = suggestedFilename.isEmpty()
                                    ? QFileInfo(savePath).fileName()
                                    : suggestedFilename;
    m_cloudDriveDownloading = true;
    if (m_cloudDrive) {
        m_cloudDrive->setDownloadInProgress(true, displayName);
    }

    QJsonObject jsonObj;
    jsonObj["tag"] = "askforcloudfile";
    jsonObj["file_id"] = fileId;
    jsonObj["account"] = m_myInfo.account;

    disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
    connect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket, Qt::QueuedConnection);

    const QJsonDocument jsonDoc(jsonObj);
    SocketDocRead::instance().sendData(jsonDoc.toJson(QJsonDocument::Compact));
}

// 查询我的网盘文件
void MainWindow::listMyCloudFiles()
{
    QJsonObject jsonObj;
    jsonObj["tag"] = "listmycloudfiles";
    jsonObj["account"] = m_myInfo.account;
    sendMessageToServer(jsonObj);
}

// 处理网盘查询结果
void MainWindow::dealCloudSearchResult(const QJsonObject &json)
{
    if (m_cloudDrive) {
        m_cloudDrive->showSearchResult(json);
    }
}

// 处理我的网盘文件列表
void MainWindow::dealListMyCloudFilesResult(const QJsonObject &json)
{
    if (!m_cloudDrive) {
        return;
    }
    if (json.value(QStringLiteral("ok")).toString() != QStringLiteral("true")) {
        m_cloudDrive->showMyFilesMessage(QStringLiteral("加载失败，请稍后重试"));
        return;
    }
    m_cloudDrive->showMyFilesList(json.value(QStringLiteral("files")).toArray());
}

// 发送文件保存完毕
void MainWindow::handleSaveDone(const QString &status)
{
    emit saveDone(status);
}

// 聊天页面项切换更新消息框视图
void MainWindow::onTalkItemCurrentChanged()
{
    const QModelIndex cur = ui->list_talks->currentIndex();
    if (!cur.isValid()) {
        ui->lab_friendname->clear();
        ui->edit_input->setEnabled(false);
        ui->but_tool_sendpic->setEnabled(false);
        ui->but_tool_sendfile->setEnabled(false);
        ui->but_tool_sendaudio->setEnabled(false);
        switchPageTo(QString());
        return;
    }

    ui->lab_friendname->setText(QStringLiteral("  ") + cur.data(Qt::DisplayRole).toString());
    ui->edit_input->setEnabled(true);
    ui->but_tool_sendpic->setEnabled(true);
    ui->but_tool_sendfile->setEnabled(true);
    ui->but_tool_sendaudio->setEnabled(true);
    clearUnread(cur.data(Qt::UserRole + 1).toString());
    if (!switchPageTo(cur.data(Qt::UserRole + 1).toString())) {
        qDebug() << "找不到聊天页面";
        ui->edit_input->setEnabled(false);
        ui->but_tool_sendpic->setEnabled(false);
        ui->but_tool_sendfile->setEnabled(false);
        ui->but_tool_sendaudio->setEnabled(false);
    }
}

// 好友列表项切换更新右侧视图
void MainWindow::onFriendItemCurrentChanged()
{
    auto index = ui->list_friends->currentIndex();
    if (!index.isValid()) {
        clearEditShow2();
        ui->edit_show2->setDefaultBack();
        ui->edit_show2->repaint();
        return;
    }
    int row = index.row();
    if (row == 0 || row == 2) return;
    else if (row == 1) {// 选择新的好友的操作 查看未处理的好友申请
        updateEditShow2New();
        return;
    }
    updateEditShow2Normal(index);// 普通好友
}

// 给服务器发送消息
void MainWindow::sendMessageToServer(const QJsonObject& ject)
{
    if(!SocketOnly::instance().isConnected()){
        return;
    }
    QJsonDocument jsonDoc(ject);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 定时更新talks表
void MainWindow::onTimerFlushTalksToDb()
{
    TalksData data;
    data.talkCache = m_talkCache;

    m_talksQueue->enqueue(data);

}
