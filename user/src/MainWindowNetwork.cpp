/**
 * @file MainWindowNetwork.cpp
 * 主窗口：登录链、好友/消息列表、实时消息与协议分发相关实现。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Dialog.h"
#include "SocketOnly.h"
#include "SocketDoc.h"
#include "SocketDepend.h"
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include "AiAnalyzeResultDialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QUuid>
#include <cstring>

namespace {
QString normalizedUuidFromJsonField(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return t;
    QUuid u = QUuid::fromString(t);
    if (u.isNull() && !t.startsWith(QLatin1Char('{')))
        u = QUuid::fromString(QLatin1Char('{') + t + QLatin1Char('}'));
    return u.isNull() ? t : u.toString(QUuid::WithoutBraces);
}

QString talkPreviewForUnread(const QString& messageType, const QString& content)
{
    if (messageType == QLatin1String("picture")) {
        return QStringLiteral("[图片消息]");
    }
    if (messageType == QLatin1String("document")) {
        return QStringLiteral("[文件消息]");
    }
    if (messageType == QLatin1String("audio")) {
        return QStringLiteral("[语音消息]");
    }
    return content;
}
}  // namespace

// 加载登录数据第一步（请求需下载头像）
void MainWindow::upload0(const QJsonObject& json)
{
    // 存储需要下载的头像URL集合
    QSet<QString> needDownloadAvatars;

    // 处理好友列表中的头像缓存检查
    if (json.contains("friends") && json["friends"].isArray()) {
        QJsonArray friendsArray = json["friends"].toArray();
        foreach (const QJsonValue& friendVal, friendsArray) {
            if (!friendVal.isObject()) {
                continue;
            }
            QJsonObject friendObj = friendVal.toObject();
            // 提取好友头像URL
            if (friendObj.contains("avator_url") && friendObj["avator_url"].isString()) {
                QString avatorUrl = friendObj["avator_url"].toString();
                if (avatorUrl.isEmpty()) {
                    continue;
                }

                QUrl url(avatorUrl);
                QString fileName = url.fileName();
                if (fileName.isEmpty()) {
                    qDebug() << "无效的头像URL，无法提取文件名:" << avatorUrl;
                    continue;
                }

                QString localAvatarPath = QDir(m_avaDir).filePath(fileName);

                // 检查本地是否存在该头像文件
                if (!QFile::exists(localAvatarPath)) {
                    needDownloadAvatars.insert(friendObj["account_number"].toString());
                }
            }
        }
    }

    QJsonObject jsonObj;
    jsonObj["tag"] = "askforloginmes1";
    jsonObj["account"] = m_myInfo.account;

    QJsonArray jsonArray;
    for (const QString& msg : needDownloadAvatars) {
        jsonArray.append(msg);
    }
    jsonObj["needdownload"] = jsonArray;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 加载登录数据第二步（好友列表、消息列表、消息记录）
void MainWindow::upload1(const QJsonObject& json)
{
    // 固定保留前 3 行（分组/标题等）；必须反复删第 3 行，避免 for(i++) 删行后索引错位导致只删一半、好友重复叠加
    m_friendItemHash.clear();
    while (m_friendModel->rowCount() > 3) {
        m_friendModel->removeRow(3);
    }
    // 从传入的 JSON 对象中获取好友数组
    QJsonArray friendsArray = json["friends"].toArray();
    // 加载好友列表
    uploadFriendList(friendsArray);
    // 加载消息列表
    uploadListMessages();
    // 加载消息记录
    uploadMessages();
    // 再次发送申请
    QJsonObject jsonObj;
    jsonObj["tag"] = "askforloginmes2";
    jsonObj["account"] = m_myInfo.account;
    // 发送数据
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 加载登录数据第三步（好友申请、未读消息）
void MainWindow::upload2(const QJsonObject& json)
{
    // loginmessage2 携带，供后续 chat_ai_analyze 请求校验（非全站 API 通用 token）。
    m_aiSessionToken = json.value(QStringLiteral("session_token")).toString();
    // 加载未读好友申请
    QJsonArray friendsRequestsArray= json["newfriends"].toArray();
    uploadFriendRequest(friendsRequestsArray);
    // 加载未读消息记录
    QJsonArray msgArray= json["unreadmessages"].toArray();
    QSet<QString> unreadMes = uploadUnreadMessages(msgArray);// 存储登录时读取的消息 一口气回发给服务器更新为已读

    emit loadFinishedSig();

    // 告知服务器登录成功，可以将这些消息更新为已读
    QJsonObject jsonObj;
    jsonObj["tag"] = "uploadSucceed";
    jsonObj["account"] = m_myInfo.account;

    QJsonArray unreadArray;
    for (const QString& msg : unreadMes) {
        unreadArray.append(msg);
    }
    jsonObj["unreadMessages"] = unreadArray;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    flushTalksCacheToDatabase();

    // 若曾在已登录状态下断线，则在此刻（同步完成、库已 flush）再清待发消息并重载顶部会话页
    if (m_needResyncTalkPagesAfterReconnect) {
        m_needResyncTalkPagesAfterReconnect = false;
        refreshTalkPagesAfterReconnectSync();
    }
}

// 保存头像
bool MainWindow::saveAvatarFromBase64(const QJsonObject& friendObject, const QString& avaDir)
{
    if (!friendObject.contains("avator") || !friendObject["avator"].isString()) {
        qDebug() << "缺少有效avator URL，无法保存头像";
        return false;
    }
    QString avatorUrl = friendObject["avator"].toString();
    QString avatorBase64 = friendObject["avatorbase64"].toString();

    QUrl url(avatorUrl);
    QString fileName = url.fileName();
    if (fileName.isEmpty()) {
        qDebug() << "无效的头像URL，无法提取文件名:" << avatorUrl;
        return false;
    }

    QDir dir(avaDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        qDebug() << "创建头像缓存目录失败:" << avaDir;
        return false;
    }
    QString localFilePath = dir.filePath(fileName);

    QByteArray avatarData = QByteArray::fromBase64(avatorBase64.toUtf8());
    if (avatarData.isEmpty()) {
        qDebug() << "base64解码失败，无法保存头像:" << fileName;
        return false;
    }

    QFile file(localFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "无法打开文件写入头像:" << localFilePath << "错误:" << file.errorString();
        return false;
    }

    qint64 bytesWritten = file.write(avatarData);
    file.close();

    if (bytesWritten != avatarData.size()) {
        qDebug() << "头像文件写入不完整:" << fileName;
        return false;
    }

    qDebug() << "头像保存成功:" << localFilePath;
    return true;
}

// 加载好友列表
void MainWindow::uploadFriendList(const QJsonArray &friendsArray)
{
    // 遍历每个好友的 JSON 对象
    for (const QJsonValue &value : friendsArray) {
        QJsonObject friendObject = value.toObject();
        if(!friendObject["avatorbase64"].toString().isEmpty()){
            saveAvatarFromBase64(friendObject, m_avaDir);
        }
        if(friendObject["account_number"] == m_myInfo.account){
            m_myInfo.account = friendObject["account_number"].toString();
            m_myInfo.gender = friendObject["gender"].toString();
            m_myInfo.name = friendObject["nickname"].toString();
            m_myInfo.signature = friendObject["signature"].toString();
            m_myInfo.avatorUrl = m_avaDir + QDir::separator() + friendObject["avator"].toString();
            AccountMessageManager::getInstance()->insert(m_myInfo.account, m_myInfo);
            initAvatar();
            continue;
        }
        AccountInfo friendInfo;
        friendInfo.account = friendObject["account_number"].toString();
        friendInfo.gender = friendObject["gender"].toString();
        friendInfo.avatorUrl = m_avaDir + QDir::separator() + friendObject["avator"].toString();
        friendInfo.name = friendObject["nickname"].toString();
        friendInfo.signature = friendObject["signature"].toString();
        addSomeoneInFriendList(friendInfo);
    }
}

// 加载好友申请
void MainWindow::uploadFriendRequest(const QJsonArray &friendsRequestArray)
{
    // 遍历每个申请者
    for (const QJsonValue &value : friendsRequestArray) {
        QJsonObject newFriendObject = value.toObject();// 将当前值转换为 QJsonObject
        qDebug()<<"发送好友申请的人有"<<newFriendObject["account_number"];
        // 创建一个AccountInfo 结构体实例
        AccountInfo newFriend;
        newFriend.account = newFriendObject["account_number"].toString();
        newFriend.name = newFriendObject["nickname"].toString();
        newFriend.avator_base64 = newFriendObject["avatorbase64"].toString(newFriendObject["avator"].toString());
        newFriend.gender = newFriendObject["gender"].toString();
        newFriend.signature = newFriendObject["signature"].toString();
        // 将新好友信息添加到好友申请列表中
        m_newFriendArray.append(newFriend);
    }
    if(friendsRequestArray.size() != 0){
        QModelIndex secondItemIndex = ui->list_friends->model()->index(1, 0);
        ui->list_friends->model()->setData(secondItemIndex, true, Qt::UserRole + 20);
        ui->list_friends->model()->dataChanged(secondItemIndex, secondItemIndex, {Qt::UserRole + 20});
    }
}

// 加载消息列表
void MainWindow::uploadListMessages()
{
    QSqlQuery qry(m_db);
    qry.prepare("SELECT friend_id, unread, latest_msg, timestamp FROM talks");
    if (!qry.exec()) {
        qDebug() << "查询talks表失败:" << qry.lastError().text();
        return;
    }

    while(qry.next()) {
        QString friendId = qry.value("friend_id").toString();       // 好友账号
        int unread = qry.value("unread").toInt();                   // 未读数
        QString latestMessage = qry.value("latest_msg").toString(); // 最新消息
        QString timestamp = qry.value("timestamp").toString();      // 消息时间戳

        auto it = AccountMessageManager::getInstance()->getInfo(friendId);

        AccountInfo friendInfo = it;
        if(friendInfo.account.isEmpty()) continue;
        addSomeoneToTalkList(friendInfo, latestMessage, "", timestamp, QString::number(unread));

        if(unread > 0) {
            liftSomebody(friendInfo.account);
        }
    }
    m_flushDbTimer->start(); // 启动定时器
}

// 加载消息记录
void MainWindow::uploadMessages()
{
    ui->but_tool_sendpic->setEnabled(false);
    ui->but_tool_sendfile->setEnabled(false);
    ui->but_tool_sendaudio->setEnabled(false);

    for (const QString &account : m_talkCache.keys()) {
        if (account == m_myInfo.account) {
            continue;
        }
        getPage(account);
    }
}

// 加载个人的消息记录
bool MainWindow::loadMessagesForAccount(QWidget *talkPage, const QString &account)
{
    if (!talkPage || account.isEmpty() || account == m_myInfo.account) {
        qDebug() << "加载消息失败：页面为空/账号为空/自身账号";
        return false;
    }

    QSqlQuery qry(m_db);
    qry.prepare("SELECT * FROM messages WHERE sender = :friend OR receiver = :friend ORDER BY received_timestamp DESC, message_id DESC LIMIT " + QString::number(CHAT_PAGE_SIZE));
    qry.bindValue(":friend", account);

    if (!qry.exec()) {
        qDebug() << "查询" << account << "消息记录失败:" << qry.lastError().text();
        return false;
    }

    struct MsgRow {
        QString sender, receiver, messageType, message, priTimestamp, uploadTimeStamp, uuidStr;
        QByteArray messageIdBin;
    };
    QVector<MsgRow> rows;
    while (qry.next()) {
        MsgRow r;
        r.sender = qry.value("sender").toString();
        r.receiver = qry.value("receiver").toString();
        r.messageType = qry.value("messagetype").toString();
        r.message = qry.value("message").toString();
        r.priTimestamp = qry.value("server_timestamp").toString();
        r.uploadTimeStamp = qry.value("received_timestamp").toString();
        r.messageIdBin = qry.value("message_id").toByteArray();
        r.uuidStr = binaryUuidToString(r.messageIdBin);
        rows.append(r);
    }

    m_lastMsgTime[account] = QString();
    for (int i = rows.size() - 1; i >= 0; --i) {
        const MsgRow &r = rows.at(i);
        addMessageTo(talkPage, r.sender, r.receiver, r.messageType, r.message,
                     r.priTimestamp, r.uploadTimeStamp, r.uuidStr, false,
                     /*insertAtTop=*/false, /*enableTime=*/true);
    }

    if (!rows.isEmpty()) {
        // 游标对应当前已加载批次里「按收到时间最早」的那条，与 ORDER BY received_timestamp DESC 一致
        m_chatOldestLoadedTime[account] = rows.last().uploadTimeStamp;
        m_chatOldestLoadedId[account] = rows.last().messageIdBin;
        m_chatHasMoreOlder[account] = (rows.size() == CHAT_PAGE_SIZE);
    } else {
        m_chatHasMoreOlder[account] = false;
        m_chatOldestLoadedTime.remove(account);
        m_chatOldestLoadedId.remove(account);
    }
    return true;
}

// 向上滚动时加载更早的消息
void MainWindow::loadOlderMessagesForAccount(QWidget *talkPage, const QString &account)
{
    if (!talkPage || account.isEmpty() || account == m_myInfo.account) {
        return;
    }
    if (m_chatLoadingOlder.value(account, false)) {
        return;
    }
    QString oldestTime = m_chatOldestLoadedTime.value(account);
    QByteArray oldestId = m_chatOldestLoadedId.value(account);
    if (oldestTime.isEmpty()) {
        return;
    }
    if (!m_chatHasMoreOlder.value(account, false)) {
        return;
    }

    m_chatLoadingOlder[account] = true;
    QListWidget *list = talkPage->findChild<QListWidget *>();
    if (!list) {
        m_chatLoadingOlder[account] = false;
        return;
    }
    const int countBefore = list->count();
    const int scrollBefore = list->verticalScrollBar()->value();

    static const int kChatTimeRowHeight = 32;
    QSqlQuery qry(m_db);
    if (!oldestId.isEmpty()) {
        qry.prepare("SELECT * FROM messages "
                    "WHERE (sender = :friend OR receiver = :friend) "
                    "AND (received_timestamp < :oldestTime OR (received_timestamp = :oldestTime AND message_id < :oldestId)) "
                    "ORDER BY received_timestamp DESC, message_id DESC LIMIT " + QString::number(CHAT_PAGE_SIZE));
        qry.bindValue(":friend", account);
        qry.bindValue(":oldestTime", oldestTime);
        qry.bindValue(":oldestId", oldestId);
    } else {
        qry.prepare("SELECT * FROM messages "
                    "WHERE (sender = :friend OR receiver = :friend) AND received_timestamp < :oldestTime "
                    "ORDER BY received_timestamp DESC, message_id DESC LIMIT " + QString::number(CHAT_PAGE_SIZE));
        qry.bindValue(":friend", account);
        qry.bindValue(":oldestTime", oldestTime);
    }

    if (!qry.exec()) {
        m_chatLoadingOlder[account] = false;
        return;
    }

    struct MsgRow {
        QString sender, receiver, messageType, message, priTimestamp, uploadTimeStamp, uuidStr;
        QByteArray messageIdBin;
    };
    QVector<MsgRow> rows;
    while (qry.next()) {
        MsgRow r;
        r.sender = qry.value("sender").toString();
        r.receiver = qry.value("receiver").toString();
        r.messageType = qry.value("messagetype").toString();
        r.message = qry.value("message").toString();
        r.priTimestamp = qry.value("server_timestamp").toString();
        r.uploadTimeStamp = qry.value("received_timestamp").toString();
        r.messageIdBin = qry.value("message_id").toByteArray();
        r.uuidStr = binaryUuidToString(r.messageIdBin);
        rows.append(r);
    }

    // 向上加载更早消息本身不参与时间戳分组，仅追加内容
    m_lastMsgTime[account] = QString();
    // 1) 先插入这一批的所有消息（从新到旧，每条 insertItem(0)，最终最早的一条在 index 0）
    for (int i = 0; i < rows.size(); ++i) {
        const MsgRow &r = rows.at(i);
        addMessageTo(talkPage, r.sender, r.receiver, r.messageType, r.message,
                     r.priTimestamp, r.uploadTimeStamp, r.uuidStr, false,
                     /*insertAtTop=*/true, /*enableTime=*/false);
    }
    // 2) 再在最顶部插入一次时间戳，这样它一定在"这一批最早的那条消息"上方
    if (!rows.isEmpty()) {
        const QDateTime dt = parseFlexibleChatTimestamp(rows.last().uploadTimeStamp);
        if (dt.isValid()) {
            QString timeText;
            formatMessageTime(dt, timeText);
            if (!timeText.isEmpty()) {
                QListWidgetItem *timeItem = new QListWidgetItem();
                QWidget *timeItemWidget = new QWidget();
                QHBoxLayout *timeLayout = new QHBoxLayout(timeItemWidget);
                timeLayout->setContentsMargins(0, 0, 0, 0);
                QLabel *timeLab = new QLabel(timeText);
                timeLab->setStyleSheet("QLabel { font-weight: bold; font-size: 11pt; text-align: center; border: none; color: rgb(60, 60, 60); }");
                timeLab->setAlignment(Qt::AlignCenter);
                timeLab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                timeLab->setFixedWidth(320);
                timeLayout->addWidget(timeLab);
                int timeH = qMax(kChatTimeRowHeight, timeLab->sizeHint().height() + 4);
                timeItem->setSizeHint(QSize(320, timeH));
                list->insertItem(0, timeItem);
                list->setItemWidget(timeItem, timeItemWidget);
            }
        }
    }

    if (!rows.isEmpty()) {
        m_chatOldestLoadedTime[account] = rows.last().uploadTimeStamp;
        m_chatOldestLoadedId[account] = rows.last().messageIdBin;
        m_chatHasMoreOlder[account] = (rows.size() == CHAT_PAGE_SIZE);
    } else {
        m_chatHasMoreOlder[account] = false;
    }

    const int insertedCount = list->count() - countBefore;
    int insertedHeight = 0;
    for (int i = 0; i < insertedCount && i < list->count(); ++i) {
        QListWidgetItem *it = list->item(i);
        if (it) {
            insertedHeight += it->sizeHint().height();
        }
    }
    list->verticalScrollBar()->setValue(scrollBefore + insertedHeight);

    m_chatLoadingOlder[account] = false;
}

// 加载未读聊天记录
QSet<QString> MainWindow::uploadUnreadMessages(const QJsonArray &messagesArray)
{
    // 收集所有涉及的sender+ 统计每个sender的未读消息数
    QSet<QString> affectedSenders;       // 去重的sender列表
    QHash<QString, int> unreadCountMap;  // 每个sender的未读消息数
    QSet<QString> unreadMes;       // 未读消息id列表

    for(const QJsonValue &value : messagesArray){
        QJsonObject json = value.toObject();
        QString uuid = json["uuid"].toString();

        QString sender = json["sender"].toString();
        QString receiver = json["receiver"].toString();
        QString messageType = json["message_type"].toString();
        QString content = json["content"].toString();
        QString msgTime = json["timestamp"].toString();
        bool fileUnavailable = json["file_unavailable"].toBool();
        msgTime.replace('T', ' ');
        unreadMes.insert(uuid);

        affectedSenders.insert(sender);

        bool isSenderExist = false;
        for (int row = 0; row < m_talkModel->rowCount(); ++row) {
            QStandardItem *itemTmp = m_talkModel->item(row);
            if (!itemTmp) {
                continue;
            }
            if (itemTmp->data(Qt::UserRole + 1).toString() == sender) {
                isSenderExist = true;
                break;
            }
        }
        if (!isSenderExist && !sender.isEmpty()) {
            addSomeoneToTalkList(AccountMessageManager::getInstance()->getInfo(sender), "", "", "", "");
        }

        QWidget *page = getPage(sender);
        if (!page) {
            continue;
        }

        if (!addMessageToDatabase(sender, receiver, messageType, content, msgTime, msgTime, uuid))
            continue;
        unreadCountMap[sender] += 1; // 仅统计本包中真正新入队的条数（与库中已有 uuid 不重复）
        // 消息入队由工作线程异步写库，此处立刻 reload 主库读不到新行；直接画到会话页，与实时收消息一致
        addMessageTo(page, sender, receiver, messageType, content, msgTime, msgTime, uuid, true,
                     false, true, fileUnavailable);
        if (m_talkCache.contains(sender)) {
            QVariantMap& talkData = m_talkCache[sender];
            talkData["latest_msg"] = talkPreviewForUnread(messageType, content);
            const QString normListTs = processTimestamp(msgTime);
            talkData["timestamp"] = normListTs.isEmpty() ? msgTime.trimmed() : normListTs;
        }
    }

    for (const QString& sender : affectedSenders) {
        if (sender.isEmpty()) {
            continue;
        }
        if (m_talkCache.contains(sender)) {
            QVariantMap& talkData = m_talkCache[sender];
            const int applied = unreadCountMap.value(sender, 0);
            if (applied > 0) {
                talkData["unread"] = talkData["unread"].toInt() + applied;
            }
            // applied==0 表示本包里没有新入队的消息（多为 uuid 已在本地），保留 uploadListMessages 从 talks 读出的未读，避免重启后角标被清零
            QStandardItem *item = nullptr;
            for (int row = 0; row < m_talkModel->rowCount(); ++row) {
                QStandardItem *itemTmp = m_talkModel->item(row);
                if (!itemTmp) {
                    continue;
                }
                if (itemTmp->data(Qt::UserRole + 1).toString() == sender) {
                    item = itemTmp;
                    break;
                }
            }
            if (item) {
                item->setData(talkData["unread"].toInt(), Qt::UserRole + 10);
            }
        }

        liftSomebody(sender);
        updateTalkList(sender);
    }
    return unreadMes;
}

// 删除好友成功
void MainWindow::deleteSucceed(const QJsonObject& json)
{
    emit deleteDone();
    Dialog* dialog = new Dialog(this);
    dialog->transText("删除好友成功!");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    deleteSomeoneInFriendList(json["account"].toString());
    deleteSomeoneInTalkList(json["account"].toString());
    // 更新视图
    // 清理旧的布局和控件
    clearEditShow2();
    ui->edit_show2->setDefaultBack();
    ui->edit_show2->repaint();
    ui->list_friends->clearSelection();
}

// 删除失败
void MainWindow::deleteFail(const QJsonObject& json)
{
    emit deleteDone();
    Dialog* dialog = new Dialog(this);
    dialog->transText("删除好友失败!");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

// 搜索好友的结果
void MainWindow::sendSearch(const QJsonObject& json)
{
    emit searchResult(json);
}

// 修改用户信息的结果
void MainWindow::changeMyInfo(const QJsonObject& json)
{
    emit changeResult(json);// 发送信号 告知修改个人资料窗口结果
    if(json["answer"] == "succeed"){// 成功则修改个人资料
        AvatarManager::getInstance()->clearAvatarCache(m_myInfo.account);
        m_myInfo.account = json["account"].toString();
        m_myInfo.gender = json["gender"].toString();
        m_myInfo.name = json["nickname"].toString();
        m_myInfo.signature = json["signature"].toString();
        m_myInfo.avatorUrl = m_avaDir + QDir::separator() + json["avator"].toString();
        saveAvatarFromBase64(json, m_avaDir);
        AccountMessageManager::getInstance()->insert(m_myInfo.account, m_myInfo);
        initAvatar();
        refreshMessageAvatarsForAccount(m_myInfo.account);
        if (ui->list_friends) ui->list_friends->viewport()->update();
        if (ui->list_talks) ui->list_talks->viewport()->update();
    }
}

// 修改账号密码第一个申请的结果
void MainWindow::changePasswordAns1(const QJsonObject& json)
{
    emit changePasswordAnswer1(json);
}

// 修改账号密码第二个申请的结果
void MainWindow::changePasswordAns2(const QJsonObject& json)
{
    if(json["answer"] == "succeed" && json["account"] == m_myInfo.account){
        showFromTray();
        Dialog* dialog = new Dialog(this);
        dialog->transText("登录凭证已过期!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
        quitApplicationFromUi();
    }
    else{
        emit changePasswordAnswer2(json);
    }
}

// 注销的结果
void MainWindow::logoutAns(const QJsonObject& json)
{
    if(json["answer"] == "success" && json["account"] == m_myInfo.account){
        showFromTray();
        Dialog* dialog = new Dialog(this);
        dialog->transText("登录凭证已过期!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
        clearAccountCache(m_myInfo.account);
        quitApplicationFromUi();
    }
    else {
        emit logoutAnswer(json);
    }
}

// 处理好友申请的结果
void MainWindow::dealFriendsRequest(const QJsonObject& json)
{
    if (json["answer"] == "succeed") {
        const QString senderAccount = json["sender"].toString();
        if (json["type"] == "accept") { // 同意申请
            AccountInfo newFriend;
            newFriend.account = json["account_number"].toString();
            newFriend.name = json["nickname"].toString();
            newFriend.gender = json["gender"].toString();
            newFriend.signature = json["signature"].toString();
            newFriend.avatorUrl = m_avaDir + QDir::separator() + json["avator"].toString();
            saveAvatarFromBase64(json, m_avaDir);
            addSomeoneInFriendList(newFriend);

            for (int i = m_newFriendArray.size() - 1; i >= 0; --i) {
                if (m_newFriendArray[i].account == senderAccount) {
                    m_newFriendArray.removeAt(i);
                    updateEditShow2New();
                    break;
                }
            }
        } else if (json["type"] == "reject") { // 拒绝申请
            for (int i = m_newFriendArray.size() - 1; i >= 0; --i) {
                if (m_newFriendArray[i].account == senderAccount) {
                    m_newFriendArray.removeAt(i);
                    updateEditShow2New();
                    break;
                }
            }
        }
    } else if (json["answer"] == "friend_limit_exceeded") { // 好友上限
        Dialog* dialog = new Dialog(this);
        dialog->transText("对方好友已上限!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}

// 接收到新的好友申请
void MainWindow::dealNewAddRequest(const QJsonObject& json)
{
    // 创建一个AccountInfo 结构体实例
    AccountInfo newFriend;
    newFriend.account = json["account_number"].toString();
    newFriend.name = json["nickname"].toString();
    newFriend.avator_base64 = json["avatorbase64"].toString(json["avator"].toString());
    newFriend.gender = json["gender"].toString();
    newFriend.signature = json["signature"].toString();
    // 将新好友信息添加到好友申请列表中
    m_newFriendArray.append(newFriend);
    QModelIndex index = ui->list_friends->model()->index(1,0);
    ui->list_friends->model()->setData(index, true, Qt::UserRole + 20);
    ui->list_friends->model()->dataChanged(index, index, {Qt::UserRole + 20});
    if(ui->list_friends->currentIndex().row() == 1){
        ui->list_friends->setCurrentIndex(index);
        updateEditShow2New();
    }
}

// 好友申请通过
void MainWindow::dealAddRequestPass(const QJsonObject& json)
{
    saveAvatarFromBase64(json, m_avaDir);
    AccountInfo friendInfo;
    friendInfo.account = json["account_number"].toString();
    friendInfo.gender = json["gender"].toString();
    friendInfo.avatorUrl = m_avaDir + QDir::separator() + json["avator"].toString();
    friendInfo.name = json["nickname"].toString();
    friendInfo.signature = json["signature"].toString();

    addSomeoneInFriendList(friendInfo);
}

// 添加某好友信息
void MainWindow::dealFriendInfo(const QJsonObject& json)
{
    dealAddRequestPass(json);

    QString targetAccount = json["account_number"].toString();
    if (targetAccount.isEmpty()) {
        return;
    }

    if (!m_friendMes.contains(targetAccount)) {
        return;
    }

    QVector<QJsonObject>& vec = m_friendMes[targetAccount];
    for (QJsonObject& msgJson : vec) {
        dealMessages(msgJson);
    }

    m_friendMes.remove(targetAccount);
}

// 处理被删除好友
void MainWindow::dealYouAreDeleted(const QJsonObject& json)
{
    deleteSomeoneInFriendList(json["account"].toString());
    deleteSomeoneInTalkList(json["account"].toString());
}

// 处理被挤下线
void MainWindow::dealYouAreKickedOffline(const QJsonObject& json)
{
    Q_UNUSED(json);
    showFromTray();
    Dialog* dialog = new Dialog(this);
    dialog->transText("登录凭证已过期!");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
    quitApplicationFromUi();
}

// 处理接收到的聊天消息
void MainWindow::dealMessages(const QJsonObject& json)
{
    QString senderAccount = json["sender"].toString();
    QString uuid = json["uuid"].toString();
    if (senderAccount.trimmed().isEmpty()) {
        return;
    }

    const QString receiverAccount = json["receiver"].toString();
    if (senderAccount == m_myInfo.account && receiverAccount == m_myInfo.account) {
        return;
    }

    // 发送给服务器 标记已读（须带接收方 account，与服务器 dealMessageRead 中 receiver_id 一致，否则服务端从不置 done，重登会重复下发未读）
    QJsonObject jsonObj;
    jsonObj["tag"] = "messageread";
    jsonObj["uuid"] = uuid;
    jsonObj["account"] = m_myInfo.account;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);

    if(!AccountMessageManager::getInstance()->containAccount(json["sender"].toString())){
        QJsonObject jsonObj;
        jsonObj["tag"] = "askforfriendinfor";
        jsonObj["account"] = m_myInfo.account;
        jsonObj["friend"] = senderAccount;
        // 发送数据
        QJsonDocument jsonDoc(jsonObj);
        QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
        SocketOnly::instance().sendData(jsonData);
        m_friendMes[senderAccount].push_back(json);
        return;
    }

    if (addSomeoneToTalkList(AccountMessageManager::getInstance()->getInfo(senderAccount),
                             QString(), QString(), QString(), QString()) < 0) {
        return;
    }
    QWidget *page = getPage(senderAccount);
    if (!page) {
        return;
    }

    QString messageType = json["messagetype"].toString().trimmed();
    if (messageType.isEmpty()) {
        messageType = QStringLiteral("text");
    }
    QString messageContent = json["messages"].toString();

    QString priTimestamp = json["timestamp"].toString();
    QString uploadTimeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    if (!addMessageToDatabase(senderAccount, receiverAccount, messageType,
                              messageContent, priTimestamp, uploadTimeStamp, uuid)) {
        return;
    }

    playSound();
    updateUnread(senderAccount);

    addMessageTo(page, senderAccount, receiverAccount, messageType,
                 messageContent, priTimestamp, uploadTimeStamp, uuid, true);

    liftSomebody(senderAccount);

    // 会话页被移除后会新建 QWidget；若左侧仍选中该好友，须把 stack 切到对应页，否则消息进了新页面但界面停在旧页
    const QModelIndex curTalk = ui->list_talks->currentIndex();
    if (curTalk.isValid() && curTalk.data(Qt::UserRole + 1).toString() == senderAccount) {
        switchPageTo(senderAccount);
    }
}

// 处理已读消息回执
void MainWindow::dealMessageHaveRead(const QJsonObject& json)
{
    QString uuidStr = json["uuid"].toString();
    if (uuidStr.isEmpty()) {
        qDebug() << "错误：JSON中未传入uuid字段，拒绝处理消息";
        return;
    }

    pushMessageToDatabase(uuidStr);
}

// 好友更新个人信息
void MainWindow::dealFriendChangeInfor(const QJsonObject& json)
{
    AccountInfo friendInfo;
    friendInfo.account = json["account"].toString();
    friendInfo.gender = json["gender"].toString();
    friendInfo.avatorUrl = m_avaDir + QDir::separator() + json["avator"].toString();
    friendInfo.name = json["nickname"].toString();
    friendInfo.signature = json["signature"].toString();

    saveAvatarFromBase64(json, m_avaDir);

    if (!friendInfo.account.isEmpty()) {
        AccountMessageManager::getInstance()->insert(friendInfo.account, friendInfo);
        refreshMessageAvatarsForAccount(friendInfo.account);
        if (ui->list_friends) ui->list_friends->viewport()->update();
        if (ui->list_talks) ui->list_talks->viewport()->update();
        auto index = ui->list_friends->currentIndex();
        QString account = index.data(Qt::UserRole + 1).toString();
        if (account == friendInfo.account)
            updateEditShow2Normal(index);  // 普通好友
    }
}

void MainWindow::onDocStreamPacket(const QByteArray& data, bool isBinary)
{
    if (isBinary) {
        onReadyReadDocBinary(data);
        return;
    }
    onReadyReadDoc(data);
}

void MainWindow::abortDocumentDownloadIoError(const QString& detail)
{
    disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
    const QString path = m_savePath;
    if (m_downloadingFile.isOpen()) {
        m_downloadingFile.close();
    }
    clearDownLoad();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
    handleSaveDone(QStringLiteral("下载失败：写入磁盘出错。\n%1").arg(detail));
    m_savePath.clear();
    m_downloadingUuid.clear();
    m_downloadingNextSeq = 0;
    m_downloadingBytes = 0;
    m_downloadingTotalBytes = 0;
    m_lastDownloadProgressPercent = -1;
    SocketDocRead::instance().scheduleDownloadSessionFinish();
}

void MainWindow::cancelDocumentDownloadDueToNetwork(const QString &reason)
{
    const bool active = !m_savePath.isEmpty() || m_downloadingFile.isOpen() || !m_downloadingUuid.isEmpty()
                        || m_downloadingTotalBytes > 0;
    if (!active)
        return;

    disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
    const QString path = m_savePath;
    if (m_downloadingFile.isOpen()) {
        m_downloadingFile.close();
    }
    clearDownLoad();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
    if (!reason.isEmpty()) {
        handleSaveDone(QStringLiteral("下载已中断：%1").arg(reason));
    }
    m_savePath.clear();
    m_downloadingUuid.clear();
    m_downloadingNextSeq = 0;
    m_downloadingBytes = 0;
    m_downloadingTotalBytes = 0;
    m_lastDownloadProgressPercent = -1;
    SocketDocRead::instance().scheduleDownloadSessionFinish();
}

// 读取服务器发送的文件
void MainWindow::onReadyReadDoc(const QByteArray& data)
{
    QJsonParseError parseErr{};
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        return;
    }

    // 处理解析成功的 JSON 对象
    QJsonObject jsonObj = jsonDoc.object();
    // 根据 "answer" 处理不同的请求
    const QString tag = jsonObj.value("tag").toString().trimmed();
    if (tag == "document_error") {
        const QString msg = jsonObj.value("message").toString();
        disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
        if (m_downloadingFile.isOpen()) m_downloadingFile.close();
        clearDownLoad();
        handleSaveDone("下载失败：\n" + msg);
        m_savePath.clear();
        m_downloadingUuid.clear();
        m_downloadingNextSeq = 0;
        m_downloadingBytes = 0;
        m_downloadingTotalBytes = 0;
        m_lastDownloadProgressPercent = -1;
        SocketDocRead::instance().scheduleDownloadSessionFinish();
        return;
    }

    if (tag == "document_begin") {
        m_downloadingUuid = normalizedUuidFromJsonField(jsonObj.value("uuid").toString());
        m_downloadingNextSeq = 0;
        m_downloadingBytes = 0;
        m_downloadingTotalBytes = jsonObj.value("total_bytes").toString().toLongLong();
        m_lastDownloadProgressPercent = -1;

        if (m_savePath.isEmpty()) {
            disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
            SocketDocRead::instance().scheduleDownloadSessionFinish();
            return;
        }
        if (m_downloadingFile.isOpen()) {
            m_downloadingFile.close();
        }
        m_downloadingFile.setFileName(m_savePath);
        if (!m_downloadingFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
            clearDownLoad();
            handleSaveDone("保存失败,路径为\n" + m_savePath);
            m_savePath.clear();
            SocketDocRead::instance().scheduleDownloadSessionFinish();
            return;
        }
        m_downloadTransferStartMs = QDateTime::currentMSecsSinceEpoch();
        setButtonProgress(ui->but_download, 0, "正在下载路径:\n" + m_savePath);
        notifyCloudDriveDownloadProgress(0);
        m_lastDownloadProgressPercent = 0;
        return;
    }

    if (tag == "document_chunk") {
        if (!m_downloadingFile.isOpen()) return;
        const QString uuid = normalizedUuidFromJsonField(jsonObj.value("uuid").toString());
        if (!m_downloadingUuid.isEmpty() && !uuid.isEmpty() && uuid != m_downloadingUuid) return;
        const QByteArray bin = QByteArray::fromBase64(jsonObj.value("data").toString().toUtf8());
        if (!bin.isEmpty()) {
            const qint64 w = m_downloadingFile.write(bin);
            if (w < 0 || w != bin.size()) {
                qWarning() << "下载写入失败，期望" << bin.size() << "实际" << w;
                abortDocumentDownloadIoError(
                    QStringLiteral("期望写入 %1 字节，实际 %2。").arg(bin.size()).arg(w));
                return;
            }
            m_downloadingBytes += w;
        }
        if (m_downloadingTotalBytes > 0) {
            const int percent = static_cast<int>((m_downloadingBytes * 100) / m_downloadingTotalBytes);
            setButtonProgress(ui->but_download, percent, "正在下载路径:\n" + m_savePath);
            notifyCloudDriveDownloadProgress(percent);
        }
        return;
    }

    if (tag == "document_end") {
        disconnect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket, this, &MainWindow::onDocStreamPacket);
        if (m_downloadingFile.isOpen()) {
            m_downloadingFile.flush();
            m_downloadingFile.close();
        }

        // 最终完整性校验：如果服务器给了总大小，则必须完全一致，否则判定下载不完整。
        if (m_downloadingTotalBytes > 0 && m_downloadingBytes != m_downloadingTotalBytes) {
            const QString path = m_savePath;
            clearDownLoad();
            if (!path.isEmpty()) QFile::remove(path);
            handleSaveDone(QStringLiteral("下载失败：文件不完整。\n期望字节数：%1\n实际字节数：%2\n请重新下载。")
                               .arg(m_downloadingTotalBytes)
                               .arg(m_downloadingBytes));
        } else {
            clearDownLoad();
            handleSaveDone("保存成功,路径为\n" + m_savePath);
        }
        m_savePath.clear();
        m_downloadingUuid.clear();
        m_downloadingNextSeq = 0;
        m_downloadingBytes = 0;
        m_downloadingTotalBytes = 0;
        m_lastDownloadProgressPercent = -1;
        SocketDocRead::instance().scheduleDownloadSessionFinish();
        return;
    }

}

// 读取文件分片
void MainWindow::onReadyReadDocBinary(const QByteArray &data)
{
    // 二进制下载分片帧：
    // body: [frameType=2][uuid(16)][seq(u32)][chunkLen(u32)][chunkBytes]
    if (data.size() < 1 + 16 + 4 + 4) return;
    const uint8_t frameType = static_cast<uint8_t>(data[0]);
    if (frameType != 2) return;

    const QByteArray uuidBin = data.mid(1, 16);
    const QUuid gotQu = QUuid::fromRfc4122(uuidBin);
    if (gotQu.isNull()) return;
    const QString uuid = gotQu.toString(QUuid::WithoutBraces);
    if (!m_downloadingUuid.isEmpty()) {
        QUuid wantQu = QUuid::fromString(m_downloadingUuid);
        if (wantQu.isNull())
            wantQu = QUuid::fromString(QStringLiteral("{") + m_downloadingUuid + QStringLiteral("}"));
        if (!wantQu.isNull() && gotQu != wantQu) return;
    }

    uint32_t seqNet = 0;
    memcpy(&seqNet, data.constData() + 1 + 16, 4);
    const qint64 seq = static_cast<qint64>(networkToHost32(seqNet));
    if (seq != m_downloadingNextSeq) {
        qWarning() << "下载分片序号不连续，expected=" << m_downloadingNextSeq << " actual=" << seq
                   << " uuid=" << uuid;
        abortDocumentDownloadIoError(
            QStringLiteral("分片序号不连续，期望 %1，实际 %2。").arg(m_downloadingNextSeq).arg(seq));
        return;
    }

    uint32_t lenNet = 0;
    memcpy(&lenNet, data.constData() + 1 + 16 + 4, 4);
    const uint32_t chunkLen = networkToHost32(lenNet);
    const int headerLen = 1 + 16 + 4 + 4;
    if (headerLen + static_cast<int>(chunkLen) > data.size()) return;
    if (chunkLen > static_cast<uint32_t>(MAX_BINARY_PACKET_SIZE)) {
        qWarning() << "下载分片 chunkLen 超限：" << chunkLen;
        return;
    }

    const QByteArray chunk = data.mid(headerLen, static_cast<int>(chunkLen));
    if (!m_downloadingFile.isOpen()) return;
    if (!chunk.isEmpty()) {
        const qint64 w = m_downloadingFile.write(chunk);
        if (w < 0 || w != chunk.size()) {
            qWarning() << "下载分片写入失败，期望" << chunk.size() << "实际" << w;
            abortDocumentDownloadIoError(
                QStringLiteral("期望写入 %1 字节，实际 %2。").arg(chunk.size()).arg(w));
            return;
        }
        m_downloadingBytes += w;
        if (m_downloadingTotalBytes > 0) {
            const int percent = static_cast<int>((m_downloadingBytes * 100) / m_downloadingTotalBytes);
            // 节流：百分比至少增加2%或已到100%时刷新；
            // 额外：当 percent 仍为 0 时，按字节步进刷新一次，避免大文件长时间显示 0。
            const bool shouldRefreshByPercent = (percent >= 100) || (percent - m_lastDownloadProgressPercent >= 2);
            const bool shouldRefreshByBytes = (percent == 0 && m_lastDownloadProgressPercent <= 0 &&
                                               (m_downloadingBytes % (256 * 1024)) < static_cast<qint64>(chunk.size()));
            if (shouldRefreshByPercent || shouldRefreshByBytes) {
                m_lastDownloadProgressPercent = qMax(m_lastDownloadProgressPercent, percent);
                setButtonProgress(ui->but_download, percent, "正在下载路径:\n" + m_savePath);
                notifyCloudDriveDownloadProgress(percent);
            }
        }
    }
    m_downloadingNextSeq += 1;
}

// 读取服务器回复数据
void MainWindow::onReadyRead(const QByteArray& data)
{
    static int s_badJsonCount = 0;
    m_recvBuffer += data;

    while (!m_recvBuffer.isEmpty()) {
        switch (m_recvState) {
        case RecvState::WaitHeader: {
            const int HeaderLen = PROTOCOL_HEADER_LEN;

            // 头部未接收完成，退出循环等待下次数据
            if (m_recvBuffer.length() < HeaderLen) {
                return;
            }

            ProtocolHeader header = bytesToHeader(m_recvBuffer.left(HeaderLen));

            if (header.version != PROTOCOL_WIRE_VERSION) {
                qWarning() << "MainWindow：收到不支持的协议版本号" << static_cast<int>(header.version);
                m_recvBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                return;
            }

            const uint8_t payloadType = header.reserved[0];
            // SocketOnly 通道只应承载 JSON；若收到 binary，直接丢弃该包并重置状态（避免被拖入错误解析）。
            if (payloadType != PAYLOAD_JSON) {
                qWarning() << "MainWindow：SocketOnly 收到非 JSON payloadType=" << static_cast<int>(payloadType)
                           << "，丢弃该连接数据";
                m_recvBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                m_expectedDataLen = 0;
                return;
            }

            // 从协议头中获取数据体长度
            m_expectedDataLen = header.dataLen;

            if (m_expectedDataLen == 0 || m_expectedDataLen > MAX_JSON_PACKET_SIZE) {
                qWarning() << "客户端收到非法数据长度：" << m_expectedDataLen;
                m_recvBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                return;
            }

            m_recvBuffer = m_recvBuffer.mid(PROTOCOL_HEADER_LEN);
            m_recvState = RecvState::WaitData;
            break;
        }

        case RecvState::WaitData: {
            // 数据体未接收完成，退出循环等待下次数据
            if (m_recvBuffer.length() < m_expectedDataLen) {
                // 额外保护：避免对端声明较大长度后持续喂数据导致缓冲无限增长。
                if (m_recvBuffer.size() > (MAX_JSON_PACKET_SIZE + PROTOCOL_HEADER_LEN) * 2) {
                    qWarning() << "客户端接收缓冲超限，丢弃并重置。buffer=" << m_recvBuffer.size()
                               << " expected=" << m_expectedDataLen;
                    m_recvBuffer.clear();
                    m_recvState = RecvState::WaitHeader;
                    m_expectedDataLen = 0;
                }
                return;
            }

            QByteArray completeData = m_recvBuffer.left(m_expectedDataLen);
            // 移除已处理的部分
            m_recvBuffer = m_recvBuffer.mid(m_expectedDataLen);

            // 解析完整JSON
            QJsonParseError parseErr;
            QJsonDocument jsonDoc = QJsonDocument::fromJson(completeData, &parseErr);
            if (jsonDoc.isNull()) {
                s_badJsonCount++;
                qWarning() << "未能解析 JSON 数据，len=" << completeData.size()
                           << " err=" << parseErr.errorString()
                           << " offset=" << parseErr.offset
                           << " badCount=" << s_badJsonCount;
                // 简单限流：连续大量坏包则直接清空缓冲，避免刷爆 CPU/日志。
                if (s_badJsonCount >= 20) {
                    qWarning() << "坏 JSON 包过多，清空接收缓冲并重置状态";
                    m_recvBuffer.clear();
                    s_badJsonCount = 0;
                }
                m_recvState = RecvState::WaitHeader;
                m_expectedDataLen = 0;
                continue;
            }
            // 成功解析后清零计数（静态计数用于快速防护，后续可按连接/时间窗口细化）。
            s_badJsonCount = 0;

            QJsonObject jsonObj = jsonDoc.object();
            if (jsonObj["tag"] == "loginmessage0") {
                upload0(jsonObj);
            }
            else if (jsonObj["tag"] == "loginmessage1") {
                upload1(jsonObj);
            }
            else if (jsonObj["tag"] == "loginmessage2") {
                upload2(jsonObj);
            }
            else if (jsonObj["tag"] == "deletefriendsucceed") {// 删除好友成功
                deleteSucceed(jsonObj);
            }
            else if (jsonObj["tag"] == "deletefriendfail") {// 删除好友失败
                deleteFail(jsonObj);
            }
            else if (jsonObj["tag"] == "serchaccount") {// 获得了查找用户的信息
                sendSearch(jsonObj);// 搜索好友的结果
            }
            else if (jsonObj["tag"] == "searchcloudfile_result") { // 网盘文件查询结果
                dealCloudSearchResult(jsonObj);
            }
            else if (jsonObj["tag"] == "listmycloudfiles_result") { // 我的网盘文件列表
                dealListMyCloudFilesResult(jsonObj);
            }
            else if (jsonObj["tag"] == "changeinformation") {// 修改用户信息完成
                changeMyInfo(jsonObj);// 修改用户信息的结果
            }
            else if (jsonObj["tag"] == "changepassword1") {// 修改账号密码第一个申请的结果
                changePasswordAns1(jsonObj);// 修改账号密码第一个申请的结果
            }
            else if (jsonObj["tag"] == "changepassword2") {// 修改账号密码第一个申请的结果
                changePasswordAns2(jsonObj);// 修改账号密码第二个申请的结果
            }
            else if (jsonObj["tag"] == "logout") {// 注销账号的结果
                logoutAns(jsonObj);
            }
            else if (jsonObj["tag"] == "updatefriendship") {// 处理好友申请的结果
                dealFriendsRequest(jsonObj);
            }
            else if (jsonObj["tag"] == "newaddrequest") {// 接收到新的好友申请 将其加入好友申请列表
                dealNewAddRequest(jsonObj);
            }
            else if (jsonObj["tag"] == "addrequestpass") {// 发送的好友申请通过 将其加入好友列表
                dealAddRequestPass(jsonObj);
            }
            else if (jsonObj["tag"] == "addfriend_answer") {// 发起添加好友请求的结果（服务器确认）
                const QString answer = jsonObj.value("answer").toString();
                const QString friendAccount = jsonObj.value("friend").toString();
                QString text;
                if (answer == "ok") {
                    text = friendAccount.isEmpty() ? "好友申请已发送" : ("已向 " + friendAccount + " 发送好友申请");
                } else if (answer == "duplicate") {
                    text = friendAccount.isEmpty() ? "好友申请已存在（请勿重复发送）" : ("你已向 " + friendAccount + " 发送过好友申请");
                } else if (answer == "invalid_params") {
                    text = "好友申请参数无效";
                } else if (answer == "db_unavailable") {
                    text = "服务器暂不可用，请稍后重试";
                } else {
                    text = "发送好友申请失败，请稍后重试";
                }
                Dialog* dialog = new Dialog(this);
                dialog->transText(text);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();
            }
            else if (jsonObj["tag"] == "friendinfor") {// 添加某好友信息 加载消息
                dealFriendInfo(jsonObj);
            }
            else if (jsonObj["tag"] == "youaredeleted") {// 处理被删除好友
                dealYouAreDeleted(jsonObj);
            }
            else if (jsonObj["tag"] == "youarekickedoffline") {// 处理被挤下线
                dealYouAreKickedOffline(jsonObj);
            }
            else if (jsonObj["tag"] == "yourmessages") {// 处理接收到的聊天消息
                dealMessages(jsonObj);
            }
            else if (jsonObj["tag"] == "messagehavedone"){
                dealMessageHaveRead(jsonObj);// 服务器已读消息 回发确认 可添加到本地数据库
            }
            else if (jsonObj["tag"] == "changeinfor"){
                dealFriendChangeInfor(jsonObj);// 好友更新个人信息
            }
            // 服务端聊天 AI 分析结果（成功弹结果窗，失败弹错误；均结束「分析中」状态）。
            else if (jsonObj["tag"] == "chat_ai_analyze_result") {
                m_aiAnalysisInProgress = false;
                if (m_aiWaitDialog) {
                    m_aiWaitDialog->close();
                }
                if (!jsonObj.value("ok").toBool()) {
                    Dialog *dialog = new Dialog(this);
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    const QString errRaw = jsonObj.value("error").toString();
                    QString errDisp = errRaw;
                    if (errRaw == QLatin1String("ai_busy")) {
                        errDisp = QStringLiteral("已有分析在进行，请稍后再试");
                    } else if (errRaw == QLatin1String("ai_rate_limited")) {
                        errDisp = QStringLiteral("请求过于频繁，请稍后再试");
                    } else if (errRaw == QLatin1String("invalid_session")) {
                        errDisp = QStringLiteral("会话已失效，请重新登录");
                    }
                    dialog->transText(QStringLiteral("AI 分析失败：%1").arg(errDisp.isEmpty() ? QStringLiteral("未知错误") : errDisp));
                    dialog->show();
                } else {
                    AiAnalyzeResultDialog *d = new AiAnalyzeResultDialog(this);
                    d->setAttribute(Qt::WA_DeleteOnClose);
                    d->setResultText(jsonObj.value("result").toString());
                    d->show();
                }
            }
            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
            break;
        }
        }
    }
}
