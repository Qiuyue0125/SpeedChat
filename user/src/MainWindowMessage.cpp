/**
 * @file MainWindowMessage.cpp
 * 聊天消息 UI、本地库读写、分页加载与重连后会话页重载。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Dialog.h"
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include "MainWindowElse.h"
#include "SocketDoc.h"
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QUuid>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>

static const int kChatListSpacing = 4;       // 消息项之间统一间距
static const int kChatMessageRowMargin = 0;  // 消息行仅依赖 QListWidget::spacing，避免边界额外空隙
static const int kChatTimeRowHeight = 32;    // 时间戳行最小高度，防止 11pt 字体被截断

namespace {
// 与 addMessageTo 一致：自己发的行 [stretch][内容][头像][spacing]，对方 [spacing][头像][内容][stretch]
QString inferMessageSenderFromRow(const QWidget* rowWidget, const QString& peerAccount, const QString& myAccount)
{
    const auto* hl = qobject_cast<const QHBoxLayout*>(rowWidget->layout());
    if (!hl) return {};
    const QList<LabelFriendAvaInMessage*> labs = rowWidget->findChildren<LabelFriendAvaInMessage*>();
    if (labs.size() != 1) return {};
    LabelFriendAvaInMessage* ava = labs.first();
    int avaIdx = -1;
    for (int i = 0; i < hl->count(); ++i) {
        QLayoutItem* item = hl->itemAt(i);
        if (item && item->widget() == ava) {
            avaIdx = i;
            break;
        }
    }
    if (avaIdx == 2) return myAccount;
    if (avaIdx == 1) return peerAccount;
    return {};
}
}  // namespace

QString MainWindow::messageUuidKey(const QString& raw) const
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return {};
    QUuid u = QUuid::fromString(t);
    if (u.isNull() && !t.startsWith(QLatin1Char('{')))
        u = QUuid::fromString(QStringLiteral("{") + t + QStringLiteral("}"));
    return u.isNull() ? QString() : u.toString(QUuid::WithoutBraces);
}

// 初始化消息列表
void MainWindow::setupMessageList(QListWidget *list)
{
    list->setViewMode(QListView::ListMode);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; } "
        "QListWidget::item { background: transparent; border: none; outline: none; } "
        "QListWidget::item:hover { background-color: transparent; } "
        "QListWidget::item:focus { outline: none; }"
        );
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(kChatListSpacing);
}

// 把某个好友添加到聊天列表
void MainWindow::addSomeoneInFriendList(const AccountInfo &friendInfo)
{
    if (friendInfo.account.isEmpty())
        return;

    AvatarManager::getInstance()->clearAvatarCache(friendInfo.account);
    AccountMessageManager::getInstance()->insert(friendInfo.account, friendInfo);

    if (QStandardItem *existing = m_friendItemHash.value(friendInfo.account, nullptr)) {
        existing->setText(friendInfo.name);
        existing->setData(friendInfo.account, Qt::UserRole + 1);
        return;
    }

    QStandardItem *item = new QStandardItem();
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled); // 仅可选中/启用
    item->setText(friendInfo.name);                          // 显示昵称
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);     // 不可编辑
    item->setData(friendInfo.account, Qt::UserRole + 1);     // 存储账号

    m_friendModel->appendRow(item);
    m_friendItemHash.insert(friendInfo.account, item);
}

// 删除好友
bool MainWindow::deleteSomeoneInFriendList(const QString &account)
{
    AccountMessageManager::getInstance()->remove(account);
    AvatarManager::getInstance()->clearAvatarCache(account);

    QStandardItem *targetItem = m_friendItemHash.take(account);
    if (!targetItem) {
        return false; // 未找到该好友
    }

    QModelIndex targetIndex = m_friendModel->indexFromItem(targetItem);
    if (targetIndex.isValid()) {
        m_friendModel->removeRow(targetIndex.row());
        ui->list_friends->update();
        return true;
    }

    return false;
}

// 删除聊天
bool MainWindow::deleteSomeoneInTalkList(const QString &account)
{
    if (account.isEmpty()) {
        qDebug() << "账号为空，删除失败";
        return false;
    }

    if (!ifTalkHaveOpened(account)) {
        qDebug() << "聊天列表中不存在" << account << "的条目，删除失败";
        return false;
    }

    if (m_talkCache.contains(account)) {
        m_talkCache.remove(account);
        qDebug() << "清理聊天缓存中" << account << "的条目";
    }

    deleteTalkWidget(account);
    ui->lab_friendname->setText("");

    QAbstractItemModel *sourceModel = m_talkFilterProxyModel->sourceModel();
    QStandardItemModel *stdModel = qobject_cast<QStandardItemModel*>(sourceModel);
    if (!stdModel) {
        qDebug() << "聊天列表源模型非QStandardItemModel类型，删除失败";
        m_talkListItems.remove(account);
        return false;
    }

    bool deleteSuccess = false;
    QStandardItem *targetItem = m_talkListItems.take(account);
    if (targetItem) {
        QModelIndex targetIndex = stdModel->indexFromItem(targetItem);
        if (targetIndex.isValid()) {
            stdModel->removeRow(targetIndex.row());

            m_talkFilterProxyModel->invalidate();
            ui->list_talks->clearSelection();

            QTimer::singleShot(0, this, [this]() {
                if (m_talkFilterProxyModel->rowCount() > 0) {
                    QModelIndex firstProxyIndex = m_talkFilterProxyModel->index(0, 0);
                    if (firstProxyIndex.isValid()) {
                        ui->list_talks->setCurrentIndex(firstProxyIndex);
                    }
                }
                ui->list_talks->update();
            });

            deleteSuccess = true;
        } else {
            delete targetItem;
            targetItem = nullptr;
            deleteSuccess = false;
        }
    } else {
        qDebug() << "哈希表中未找到" << account << "对应的条目，删除失败";
        deleteSuccess = false;
    }

    if (!deleteSuccess) {
        qDebug() << "删除" << account << "的聊天列表条目最终失败";
    }

    return deleteSuccess;
}

// 判断聊天是否已打开
bool MainWindow::ifTalkHaveOpened(const QString &account)
{
    if (m_talkListItems.contains(account)) {
        return true;
    }

    return false;
}

// 添加聊天到列表
int MainWindow::addSomeoneToTalkList(const AccountInfo &friendMessage,
                                     const QString& message,
                                     const QString& msgType,
                                     const QString& date,
                                     const QString& unread)
{
    if (friendMessage.account.trimmed().isEmpty()) return -1;

    if (ifTalkHaveOpened(friendMessage.account)) {
        QStandardItem *existItem = m_talkListItems.value(friendMessage.account);
        const QModelIndex existIdx = existItem ? m_talkModel->indexFromItem(existItem) : QModelIndex();
        if (existItem && existIdx.isValid()) {
            // 列表项仍在模型中：补齐可能被异常路径清掉的缓存，否则 updateUnread/updateTalkListFree 直接 return 造成状态错乱
            if (!m_talkCache.contains(friendMessage.account)) {
                QVariantMap repair;
                repair["friend_id"] = friendMessage.account;
                repair["unread"] = unread.toInt();
                QString preview;
                if (msgType == "picture") {
                    preview = "[图片消息]";
                } else if (msgType == "audio") {
                    preview = "[语音消息]";
                } else if (msgType == "document") {
                    preview = "[文件消息]";
                } else {
                    preview = message;
                }
                repair["latest_msg"] = preview;
                repair["timestamp"] = date;
                m_talkCache.insert(friendMessage.account, repair);
            }
            return existIdx.row();
        }
        m_talkListItems.remove(friendMessage.account);
        m_talkCache.remove(friendMessage.account);
        for (int row = m_talkModel->rowCount() - 1; row >= 0; --row) {
            QStandardItem *it = m_talkModel->item(row);
            if (it && it->data(Qt::UserRole + 1).toString() == friendMessage.account) {
                m_talkModel->removeRow(row);
            }
        }
    }

    QVariantMap talkData;
    talkData["friend_id"] = friendMessage.account;
    talkData["unread"] = unread.toInt();
    QString msg;
    if (msgType == "picture") {msg = "[图片消息]";}
    else if (msgType == "audio") {msg = "[语音消息]";}
    else if (msgType == "document") {msg = "[文件消息]";}
    else  msg = message;
    talkData["latest_msg"] = msg;
    talkData["timestamp"] = date;
    m_talkCache.insert(friendMessage.account, talkData);

    QStandardItem *item = new QStandardItem();
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    {
        const QString display = friendMessage.name.isEmpty()
                                    ? friendMessage.account
                                    : friendMessage.name;
        item->setText(display);
    }
    item->setFlags(item->flags() & ~Qt::ItemIsEditable); // 不可编辑

    item->setData(friendMessage.account, Qt::UserRole + 1);    // 账号
    item->setData(message, Qt::UserRole + 5);                  // 最新消息
    item->setData(date, Qt::UserRole + 6);                     // 时间戳
    item->setData(unread.toInt(), Qt::UserRole + 10);          // 未读数

    m_talkModel->appendRow(item);
    m_talkListItems.insert(friendMessage.account, item);

    return m_talkModel->rowCount() - 1;
}

// 打开聊天
int MainWindow::selectSomeoneInTalkList(const QString &account)
{
    ui->line_search->setText("搜索");
    ui->line_search->clearFocus();

    if (!ifTalkHaveOpened(account)) {
        return -1;
    }

    QStandardItem *targetItem = m_talkListItems.value(account);
    if (!targetItem) {
        return -1;
    }

    QModelIndex sourceIndex = m_talkModel->indexFromItem(targetItem);
    if (sourceIndex.isValid()) {
        QModelIndex proxyIndex = m_talkFilterProxyModel->mapFromSource(sourceIndex);
        if (proxyIndex.isValid()) {
            ui->list_talks->setCurrentIndex(proxyIndex);
        }
        return sourceIndex.row();
    }

    return -1;
}

// 发送消息
void MainWindow::sendMessageToFriend(const QString &account)
{
    if(!AccountMessageManager::getInstance()->getInfo(account).account.isEmpty()){// 确定有这个好友
        AccountInfo info = AccountMessageManager::getInstance()->getInfo(account);
        if(info.account.isEmpty()) return;
        addSomeoneToTalkList(info, QString(), QString(), QString(), QString());
        selectSomeoneInTalkList(account);
        updateTalkList(account);
        ui->but_chat->click();
    }
    else{// 没有这个好友
        Dialog* dialog = new Dialog(this);
        dialog->transText("他不在您的好友列表当中!");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
}

// 切换聊天页面
bool MainWindow::switchPageTo(const QString &friendId)
{
    if (friendId.isEmpty()) {
        if (!m_emptyTalkPage) {
            m_emptyTalkPage = new QWidget(ui->stack_talks);
            m_emptyTalkPage->setObjectName("EmptyTalkPage"); // 标记空页面，方便识别
            ui->stack_talks->addWidget(m_emptyTalkPage);
        }

        int emptyIndex = ui->stack_talks->indexOf(m_emptyTalkPage);
        ui->stack_talks->setCurrentIndex(emptyIndex);

        return false;
    }

    QWidget *targetPage = getPage(friendId);
    if (targetPage) {
        // 到目标页面 切换 + 滚动到底部
        int index = ui->stack_talks->indexOf(targetPage);
        ui->stack_talks->setCurrentIndex(index);
        QListWidget *list = targetPage->findChild<QListWidget *>();
        if (list) {
            // 延迟到布局完成后再滚动；会话页可能已被移除(deleteLater)，必须用 QPointer 避免野指针崩溃
            QPointer<QListWidget> listGuard(list);
            QTimer::singleShot(0, this, [listGuard]() {
                if (listGuard.isNull()) {
                    return;
                }
                listGuard->scrollToBottom();
                listGuard->scrollToBottom();
            });
        }
        refreshLazyMessageAvatarsOnPage(targetPage);
        return true; // 切换到有效页面，返回true
    }

    return false;
}

// 获取某个聊天页面
QWidget* MainWindow::getPage(const QString &friendId)
{
    if(friendId.isEmpty()) return nullptr;
    if (m_talksItem.contains(friendId)) {
        QWidget *cachedWidget = m_talksItem.value(friendId);
        if (cachedWidget && ui->stack_talks->indexOf(cachedWidget) != -1) {
            return cachedWidget;
        } else {
            m_talksItem.remove(friendId);
        }
    }

    QWidget *newPage = new QWidget();
    newPage->setObjectName(friendId);

    QListWidget *list = new QListWidget();
    setupMessageList(list);
    list->setObjectName("notime");

    QVBoxLayout *layout0 = new QVBoxLayout(newPage);
    layout0->addWidget(list);
    newPage->setLayout(layout0);

    ui->stack_talks->addWidget(newPage);
    m_talksItem.insert(friendId, newPage);
    if (m_lazyStaleMessageAvatarAccounts.contains(m_myInfo.account))
        m_lazySelfAvatarPagesRefreshed.clear();
    if (!loadMessagesForAccount(newPage, friendId)) {
        qDebug() << "创建页面成功，但加载" << friendId << "的历史消息失败";
    }

    QScrollBar *vbar = list->verticalScrollBar();
    QPointer<QWidget> pageGuard(newPage);
    // 只捕获 pageGuard；垂直条异常为空时不连接，避免对空指针 connect
    if (vbar) {
        connect(vbar, &QScrollBar::valueChanged, this, [this, pageGuard, friendId](int value) {
            if (pageGuard.isNull()) {
                return;
            }
            QListWidget *lw = pageGuard->findChild<QListWidget *>();
            if (!lw) {
                return;
            }
            QScrollBar *vs = lw->verticalScrollBar();
            if (!vs) {
                return;
            }
            if (!m_chatHasMoreOlder.value(friendId, false) || m_chatLoadingOlder.value(friendId, false)) {
                return;
            }
            const int lo = vs->minimum();
            const int hi = vs->maximum();
            const bool nearTop = (hi <= lo) || (value <= lo + 80) || (value <= lo + (hi - lo) * 15 / 100);
            if (!nearTop) {
                return;
            }
            loadOlderMessagesForAccount(pageGuard.data(), friendId);
        });
    }

    return newPage;
}

// 删除某个聊天页面
bool MainWindow::deleteTalkWidget(const QString &account)
{
    if (account.isEmpty()) {
        qDebug() << "删除聊天页面失败：账号为空";
        return false;
    }

    removeRecentMessageUuidKeysForFriend(account);

    QWidget *targetWidget = m_talksItem.take(account);
    if (!targetWidget) {
        qDebug() << "删除聊天页面失败：哈希表中未找到" << account << "对应的页面";
        return false;
    }

    // 先断开本会话页内滚动条等到 MainWindow 的槽，再 removeWidget/deleteLater，避免销毁后仍触发 lambda（第二次关闭易崩）
    const QList<QScrollBar *> scrollBars = targetWidget->findChildren<QScrollBar *>();
    for (QScrollBar *sb : scrollBars) {
        if (sb) {
            QObject::disconnect(sb, nullptr, this, nullptr);
        }
    }

    m_lazySelfAvatarPagesRefreshed.remove(targetWidget);

    m_chatOldestLoadedTime.remove(account);
    m_chatOldestLoadedId.remove(account);
    m_chatHasMoreOlder.remove(account);
    m_chatLoadingOlder.remove(account);
    m_lastMsgTime.remove(account);

    int widgetIndex = ui->stack_talks->indexOf(targetWidget);
    if (widgetIndex != -1) {
        ui->stack_talks->removeWidget(targetWidget);
    }

    targetWidget->deleteLater();

    qDebug() << "成功删除" << account << "对应的聊天页面";
    return true;
}

void MainWindow::removeRecentMessageUuidKeysForFriend(const QString& friendAccount)
{
    if (friendAccount.isEmpty() || !m_db.isOpen()) {
        return;
    }
    QSqlQuery qry(m_db);
    qry.prepare(QStringLiteral("SELECT message_id FROM messages WHERE sender = :f OR receiver = :f"));
    qry.bindValue(QStringLiteral(":f"), friendAccount);
    if (!qry.exec()) {
        return;
    }
    while (qry.next()) {
        const QString u = binaryUuidToString(qry.value(0).toByteArray());
        const QString uk = messageUuidKey(u);
        if (!uk.isEmpty()) {
            m_recentMessageUuidKeys.remove(uk);
        }
    }
}

// 从本地库重绘某会话页消息列表，并校正左侧预览（与 messages / talks 表一致）
void MainWindow::reloadTalkPageFromDatabase(const QString& account)
{
    if (account.isEmpty()) {
        return;
    }
    removeRecentMessageUuidKeysForFriend(account);
    QWidget *page = m_talksItem.value(account, nullptr);
    if (!page) {
        return;
    }
    if (QListWidget *list = page->findChild<QListWidget *>()) {
        list->clear();
        list->setObjectName(QStringLiteral("notime"));
    }
    m_lastMsgTime.remove(account);
    m_chatOldestLoadedTime.remove(account);
    m_chatOldestLoadedId.remove(account);
    m_chatHasMoreOlder.remove(account);
    m_chatLoadingOlder.remove(account);
    loadMessagesForAccount(page, account);
    restoreTalkListPreviewFromDatabaseForFriend(account);
}

// 用数据库中该会话最新消息（或 talks 行）刷新 m_talkCache 与列表项上的预览文案
void MainWindow::restoreTalkListPreviewFromDatabaseForFriend(const QString& friendId)
{
    if (friendId.isEmpty() || !m_talkCache.contains(friendId) || !m_db.isOpen()) {
        return;
    }

    QSqlQuery qry(m_db);
    qry.prepare(QStringLiteral(
        "SELECT messagetype, message, received_timestamp FROM messages "
        "WHERE sender = :f OR receiver = :f "
        "ORDER BY received_timestamp DESC, message_id DESC LIMIT 1"));
    qry.bindValue(QStringLiteral(":f"), friendId);
    if (qry.exec() && qry.next()) {
        const QString mt = qry.value(0).toString().trimmed().toLower();
        const QString content = qry.value(1).toString();
        const QString ts = qry.value(2).toString();
        QString preview;
        if (mt == QLatin1String("picture")) {
            preview = QStringLiteral("[图片消息]");
        } else if (mt == QLatin1String("document")) {
            preview = QStringLiteral("[文件消息]");
        } else if (mt == QLatin1String("audio")) {
            preview = QStringLiteral("[语音消息]");
        } else {
            preview = content;
        }
        QVariantMap& td = m_talkCache[friendId];
        td[QStringLiteral("latest_msg")] = preview;
        td[QStringLiteral("timestamp")] = ts;
        updateTalkList(friendId);
        return;
    }

    qry.prepare(QStringLiteral("SELECT latest_msg, timestamp FROM talks WHERE friend_id = :f"));
    qry.bindValue(QStringLiteral(":f"), friendId);
    if (qry.exec() && qry.next()) {
        QVariantMap& td = m_talkCache[friendId];
        td[QStringLiteral("latest_msg")] = qry.value(0).toString();
        td[QStringLiteral("timestamp")] = qry.value(1).toString();
        updateTalkList(friendId);
    }
}

// 丢弃所有「已发 UI、等服务器回执再入库」的缓存，避免重连后与真实状态不一致
void MainWindow::discardPendingOutboundMessagesAfterReconnect()
{
    const QList<QString> keys = m_messageHash.keys();
    for (const QString& uuid : keys) {
        const QString uk = messageUuidKey(uuid);
        if (!uk.isEmpty()) {
            m_recentMessageUuidKeys.remove(uk);
        }
    }
    m_messageHash.clear();
    m_pendingMessageAckDeadlineMs.clear();
    if (m_pendingMessageAckSweepTimer) {
        m_pendingMessageAckSweepTimer->stop();
    }
}

// 在 upload2（登录同步）完成后调用：先收集 m_messageHash 中的会话对端，再清哈希，并对已打开页按 DB 重载
void MainWindow::refreshTalkPagesAfterReconnectSync()
{
    if (!m_didFirstHavelogin || m_myInfo.account.isEmpty()) {
        return;
    }

    QSet<QString> pendingPeerAccounts;
    for (auto it = m_messageHash.constBegin(); it != m_messageHash.constEnd(); ++it) {
        const MessageData& md = it.value();
        const QString peer = (md.sender == m_myInfo.account) ? md.receiver : md.sender;
        if (!peer.isEmpty() && peer != m_myInfo.account) {
            pendingPeerAccounts.insert(peer);
        }
    }

    discardPendingOutboundMessagesAfterReconnect();

    for (const QString& friendId : pendingPeerAccounts) {
        if (m_talksItem.contains(friendId)) {
            reloadTalkPageFromDatabase(friendId);
        }
    }
    // upload2 里此前已 flush 过 talks；校正预览后写回库，避免乐观更新残留在 talks 表
    flushTalksCacheToDatabase();
}

// 处理时间戳
QString MainWindow::processTimestamp(const QString& rawTimestamp)
{
    if (rawTimestamp.isEmpty()) {
        return QString();
    }
    const QDateTime dt = parseFlexibleChatTimestamp(rawTimestamp);
    if (!dt.isValid()) {
        return QString();
    }
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// 设置换行
void MainWindow::resetText(QLabel *label, const QString &text)
{
    if (text.isEmpty() || label == nullptr)
    {
        label->setText("");
        return;
    }

    QFontMetrics fm(label->font());
    int maxWidth = label->maximumWidth();
    if (maxWidth <= 0) {
        label->setText(text);
        return;
    }

    int usableWidth = maxWidth / 2;
    int xWidth = fm.horizontalAdvance(QChar(u'x'));
    int nMax = xWidth > 0 ? (usableWidth / xWidth) : 10;
    nMax = nMax / 2;
    nMax = qMax(nMax, 8);

    QString res;
    QString target = text;
    int nCount = target.size();

    while (nMax < nCount)
    {
        QString temp = target.left(nMax);
        temp.append("\n");
        res.append(temp);
        target.remove(0, nMax);
        nCount -= nMax;
    }
    res.append(target);

    label->setWordWrap(false);
    label->setText(res);
}

// 添加消息到页面
void MainWindow::addMessageTo(const QWidget *page, const QString &sender, const QString &receiver,
                              const QString &messageType,const QString &message, const QString& priTimestamp,
                              const QString& uploadTimeStamp, const QString &uuid, bool save,
                              bool insertAtTop, bool enableTime, bool fileUnavailable)
{
    if (!page) {
        return;
    }
    if (!save) {
        const QString uk = messageUuidKey(uuid);
        if (!uk.isEmpty()) {
            if (m_recentMessageUuidKeys.contains(uk)) return;
            m_recentMessageUuidKeys.insert(uk);
        }
    }

    QString timestamp = processTimestamp(uploadTimeStamp);
    QString msg;
    if (messageType == "picture") {
        msg = "[图片消息]";
    } else if (messageType == "document") {
        msg = "[文件消息]";
    } else if (messageType == "audio") {
        msg = "[语音消息]";
    } else {
        msg = message;
    }

    if (!insertAtTop) {
        updateTalkListFree(page->objectName(), msg, timestamp);
    }

    QListWidget *list = page->findChild<QListWidget *>();
    if (!list) {
        qDebug() << "未找到QListWidget控件";
        return;
    }

    const QString friendId = (sender == m_myInfo.account) ? receiver : sender;
    QString preTime = m_lastMsgTime.value(friendId);
    QString time;
    if (enableTime) {
        printTimeOrNot(uploadTimeStamp, preTime, time);
    }

    QListWidgetItem *item = new QListWidgetItem();
    QListWidgetItem *deferredTimeItem = nullptr;
    QWidget *itemWidget = new QWidget();
    QHBoxLayout *messageLayout = new QHBoxLayout(itemWidget);
    messageLayout->setContentsMargins(0, 0, 0, 0);

    if (!time.isEmpty()) {
        QListWidgetItem *timeItem = new QListWidgetItem();
        QWidget *timeItemWidget = new QWidget();
        QHBoxLayout *timeLayout = new QHBoxLayout(timeItemWidget);
        QLabel *timeLab = new QLabel(time);
        timeLab->setStyleSheet("QLabel { font-weight: bold; font-size: 11pt; text-align: center; border: none; color: rgb(60, 60, 60); }");
        timeLab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        timeLab->setAlignment(Qt::AlignCenter);
        timeLab->setFixedWidth(320);
        timeLayout->addWidget(timeLab, Qt::AlignCenter);
        int timeH = qMax(kChatTimeRowHeight, timeLab->sizeHint().height() + 4);
        timeItem->setSizeHint(QSize(320, timeH));
        if (insertAtTop) {
            deferredTimeItem = timeItem;
        } else {
            list->addItem(timeItem);
        }
        list->setItemWidget(timeItem, timeItemWidget);
    }
    // 仅对"追加到末尾"的消息更新上一条时间；向上加载旧消息（insertAtTop）
    // 只影响历史，不应该改变后续新消息的时间分组逻辑
    if (!insertAtTop) {
        m_lastMsgTime[friendId] = uploadTimeStamp;
    }

    // 创建发送者头像
    LabelFriendAvaInMessage *senderAva = new LabelFriendAvaInMessage;
    senderAva->setScaledContents(true);
    senderAva->setFixedSize(40, 40);// 头像大小
    senderAva->setProperty("msgSenderAccount", sender);
    senderAva->setCursor(Qt::PointingHandCursor);
    if (sender == m_myInfo.account){
        connect(senderAva, &LabelFriendAvaInMessage::showMessage, [=](){
            changeInfo();
        });
    }
    else{
        connect(senderAva, &LabelFriendAvaInMessage::showMessage, [=](){
            QJsonObject json;
            json["tag"] = "message";
            listtalkChoice(json);
        });
    }

    // 获取相应的头像
    QPixmap pix = AvatarManager::getInstance()->loadAvator(sender, QSize(40,40));
    senderAva->setPixmap(pix);

    int messageHeight = 0;
    const QString mt = messageType.trimmed().toLower();

    if (mt == QLatin1String("text"))
    {
        QLabel *messageLabel = new QLabel();
        messageLabel->setContextMenuPolicy(Qt::NoContextMenu);
        messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        messageLabel->setStyleSheet("background-color: rgb(167, 214, 255); "
                                    "color: black; border-radius: 10px; "
                                    "padding: 5px; line-height: 2; text-align: center; "
                                    "font: 450 12pt 'Microsoft YaHei UI Light';"); // 行高
        messageLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
        messageLabel->setContentsMargins(5,5,5,5);
        messageLabel->setMaximumWidth(400);// 设置最大宽度，防止消息过宽
        resetText(messageLabel, message);

        // 根据sender进行布局判断
        if (sender == m_myInfo.account) {
            messageLayout->addStretch(); // 左侧留空
            messageLayout->addWidget(messageLabel);
            messageLayout->addWidget(senderAva);
            messageLayout->addSpacing(10);
        } else {
            messageLayout->addSpacing(10);
            messageLayout->addWidget(senderAva);
            messageLayout->addWidget(messageLabel);
            messageLayout->addStretch(); // 右侧留空
        }
        messageHeight = messageLabel->sizeHint().height();
    }
    else if (mt == QLatin1String("picture"))
    {
        if (fileUnavailable) {
            QLabel *placeholderLabel = new QLabel("[图片无法加载]");
            placeholderLabel->setStyleSheet("background-color: rgb(200, 200, 200); color: gray; "
                                            "border-radius: 5px; padding: 10px; font: 12pt;");
            placeholderLabel->setFixedSize(120, 80);
            if (sender == m_myInfo.account) {
                messageLayout->addStretch();
                messageLayout->addWidget(placeholderLabel);
                messageLayout->addWidget(senderAva);
                messageLayout->addSpacing(10);
            } else {
                messageLayout->addSpacing(10);
                messageLayout->addWidget(senderAva);
                messageLayout->addWidget(placeholderLabel);
                messageLayout->addStretch();
            }
            messageHeight = 80;
        } else {
        QString friendId = (sender == m_myInfo.account) ? receiver : sender;
        QPixmap pix = AvatarManager::base64ToPixmap(message);
        QString savePath = savePic(save, friendId, pix, uuid);

        ImageLabel *picLabel = new ImageLabel(savePath);

        pix = pix.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        picLabel->setFixedSize(pix.size() + QSize(10, 10));
        picLabel->setPixmap(pix);
        picLabel->setStyleSheet("background-color: rgb(167, 214, 255); "
                                "border-radius: 5px; "
                                /*"padding: 0px;"*/);
        picLabel->setScaledContents(false);
        picLabel->setAlignment(Qt::AlignCenter);

        if (sender == m_myInfo.account) {
            messageLayout->addStretch(); // 左侧留空
            messageLayout->addWidget(picLabel);
            messageLayout->addWidget(senderAva);
            messageLayout->addSpacing(10);
        } else {
            messageLayout->addSpacing(10);
            messageLayout->addWidget(senderAva);
            messageLayout->addWidget(picLabel);
            messageLayout->addStretch(); // 右侧留空
        }
        messageHeight = picLabel->height();
        }
    }
    else if (mt == QLatin1String("document"))
    {
        QPushButton *document = new QPushButton();
        document->setFixedSize(200, 60);
        document->setStyleSheet("background-color: rgb(145, 190, 230); "
                                "color: black; border-radius: 10px; "
                                "line-height: 2; text-align: left; "
                                "font: 480 13pt 'Microsoft YaHei UI Light';"
                                "padding: 7px;");
        document->setIcon(QIcon(":/pictures/icon_document.png"));
        document->setText(message.left(14));

        if(sender != m_myInfo.account){
        connect(document, &QPushButton::clicked, [this, message, uuid]() {
            if(!m_savePath.isEmpty()){
                Dialog* dialog = new Dialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->transText("请不要重复下载,正在下载文件:\n" + m_savePath);
                dialog->show();
                return;
            }
            QJsonObject jsonObj;
            jsonObj["tag"] = "askfordocument";
            jsonObj["uuid"] = uuid;
            jsonObj["account"] = m_myInfo.account;
            qDebug()<<uuid;
            QJsonDocument jsonDoc(jsonObj);
            QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);

            // 选择保存路径
            m_savePath = QFileDialog::getSaveFileName(nullptr, tr("Save File"), message);
            if (m_savePath.isEmpty()) {
                qDebug() << "保存文件被取消";
                return;
            }

            setDownLoad(m_savePath);
            // Dialog* dialog = new Dialog(this);
            // dialog->transText("文件开始下载,请稍等!");
            // dialog->setAttribute(Qt::WA_DeleteOnClose);
            // dialog->show();

            // 单一信号保证 JSON/二进制分片在主线程的投递顺序与解析顺序一致
            connect(&SocketDocRead::instance(), &SocketDocRead::docStreamPacket,
                    this, &MainWindow::onDocStreamPacket,
                    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
            SocketDocRead::instance().sendData(jsonData);
        });
        }

        // 根据sender进行布局判断
        if (sender == m_myInfo.account) {
            messageLayout->addStretch();// 左侧留空
            messageLayout->addWidget(document);
            messageLayout->addWidget(senderAva);
            messageLayout->addSpacing(10);
        } else {
            document->setCursor(Qt::PointingHandCursor);// 对方发的则可点击
            messageLayout->addSpacing(10);
            messageLayout->addWidget(senderAva);
            messageLayout->addWidget(document);
            messageLayout->addStretch();// 右侧留空
        }
        messageHeight = 80;
    }
    else if (mt == QLatin1String("audio"))
    {
        if (fileUnavailable) {
            QLabel *placeholderLabel = new QLabel("[语音无法加载]");
            placeholderLabel->setStyleSheet("background-color: rgb(200, 200, 200); color: gray; "
                                            "border-radius: 5px; padding: 10px; font: 12pt;");
            placeholderLabel->setFixedSize(120, 50);
            if (sender == m_myInfo.account) {
                messageLayout->addStretch();
                messageLayout->addWidget(placeholderLabel);
                messageLayout->addWidget(senderAva);
                messageLayout->addSpacing(10);
            } else {
                messageLayout->addSpacing(10);
                messageLayout->addWidget(senderAva);
                messageLayout->addWidget(placeholderLabel);
                messageLayout->addStretch();
            }
            messageHeight = 50;
        } else {
        QByteArray audioData = QByteArray::fromBase64(message.toLatin1());
        QString time = getAudioTime(audioData);
        QString friendId = (sender == m_myInfo.account) ? receiver : sender;
        QString savePath = saveAudio(save, friendId, audioData, uuid);
        AudioLabel *audioLabel = new AudioLabel(time, savePath);
        connect(audioLabel, &AudioLabel::startAudio, this,&MainWindow::startAudio);
        audioLabel->setStyleSheet("background-color: rgb(145, 190, 230); "
                                  "border-radius: 10px; "
                                  "font: 450 12pt 'Microsoft YaHei UI Light';"
                                  );
        audioLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        audioLabel->setContentsMargins(5,5,5,5);
        // 根据sender进行布局判断
        if (sender == m_myInfo.account) {
            messageLayout->addStretch(); // 左侧留空
            messageLayout->addWidget(audioLabel);
            messageLayout->addWidget(senderAva);
            messageLayout->addSpacing(10);
        } else {
            messageLayout->addSpacing(10);
            messageLayout->addWidget(senderAva);
            messageLayout->addWidget(audioLabel);
            messageLayout->addStretch(); // 右侧留空
        }
        messageHeight = audioLabel->sizeHint().height();
        }
    }
    else
    {
        QLabel *messageLabel = new QLabel();
        messageLabel->setContextMenuPolicy(Qt::NoContextMenu);
        messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        messageLabel->setWordWrap(true);
        messageLabel->setStyleSheet(
            "background-color: rgb(167, 214, 255); "
            "color: black; border-radius: 10px; "
            "padding: 5px; line-height: 2; text-align: center; "
            "font: 450 12pt 'Microsoft YaHei UI Light';");
        messageLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
        messageLabel->setContentsMargins(5, 5, 5, 5);
        messageLabel->setMaximumWidth(400);
        const QString shown = message.isEmpty()
                                  ? QStringLiteral("[%1]").arg(messageType.trimmed().isEmpty()
                                                                    ? QStringLiteral("消息")
                                                                    : messageType.trimmed())
                                  : message;
        resetText(messageLabel, shown);
        if (sender == m_myInfo.account) {
            messageLayout->addStretch();
            messageLayout->addWidget(messageLabel);
            messageLayout->addWidget(senderAva);
            messageLayout->addSpacing(10);
        } else {
            messageLayout->addSpacing(10);
            messageLayout->addWidget(senderAva);
            messageLayout->addWidget(messageLabel);
            messageLayout->addStretch();
        }
        messageHeight = messageLabel->sizeHint().height();
    }

    messageLayout->setAlignment(Qt::AlignVCenter);
    int senderHeight = senderAva->height();
    int totalHeight = qMax(senderHeight, messageHeight) + kChatMessageRowMargin;
    item->setSizeHint(QSize(0, totalHeight));
    if (insertAtTop) {
        list->insertItem(0, item);
        list->setItemWidget(item, itemWidget);
        if (deferredTimeItem) {
            list->insertItem(0, deferredTimeItem);
        }
    } else {
        list->addItem(item);
        list->setItemWidget(item, itemWidget);
    }
    if (!insertAtTop) {
        list->scrollToBottom();
    }
}

// 保存图片
QString MainWindow::savePic(bool save, const QString &friendId, const QPixmap& pix, const QString& messageId)
{
    // 构造最终的保存目录 m_apDir/friendId/pic
    QDir baseDir(m_apDir);
    QString picDirPath = baseDir.filePath(QString("%1/pic").arg(friendId));

    // 构造图片文件的完整路径 picDirPath/messageId.png
    QString picFilePath = QDir(picDirPath).filePath(QString("%1.png").arg(messageId));

    // 仅当save为true时执行保存逻辑
    if (save) {
        QDir dir;
        bool dirCreated = dir.mkpath(picDirPath);
        if (!dirCreated) {
            qWarning() << "[savePic] 目录创建失败：" << picDirPath;
            return picFilePath;
        }

        bool picSaved = pix.save(picFilePath, "PNG");
        if (!picSaved) {
            qWarning() << "[savePic] 图片保存失败：" << picFilePath;
        }
    }

    return picFilePath;
}

// 保存语音
QString MainWindow::saveAudio(bool save, const QString &friendId, const QByteArray& audioData, const QString& messageId)
{
    QDir baseDir(m_apDir);
    QString audioDirPath = baseDir.filePath(QString("%1/audio").arg(friendId));

    QString audioFilePath = QDir(audioDirPath).filePath(QString("%1.audio").arg(messageId));

    if (save) {
        if (audioData.isEmpty()) {
            return audioFilePath;
        }

        QDir dir;
        bool dirCreated = dir.mkpath(audioDirPath);
        if (!dirCreated) {
            return audioFilePath;
        }

        // 写入语音数据到文件
        QFile audioFile(audioFilePath);
        if (!audioFile.open(QIODevice::WriteOnly)) {
            return audioFilePath;
        }

        qint64 writtenBytes = audioFile.write(audioData);
        audioFile.close();

        if (writtenBytes != audioData.size()) {
        } else {
        }
    }

    return audioFilePath;
}

// 把聊天记录添加到本地数据库
bool MainWindow::addMessageToDatabase(const QString &sender,const QString &receiver, const QString &messageType,
                                      const QString &message,  const QString& priTimestamp, const QString& uploadTimeStamp, const QString &uuid)
{
    const QString uk = messageUuidKey(uuid);
    if (uk.isEmpty()) {
        qDebug() << "错误：传入的uuid格式无效，值为：" << uuid;
        return false;
    }
    if (m_recentMessageUuidKeys.contains(uk)) {
        return false;
    }

    QUuid id = QUuid::fromString(uk);
    if (id.isNull()) id = QUuid::fromString(QStringLiteral("{") + uk + QStringLiteral("}"));
    if (id.isNull()) {
        qDebug() << "错误：UUID 解析失败，值为：" << uuid;
        return false;
    }

    QByteArray messageIdBin = id.toRfc4122();
    if (messageIdBin.isEmpty()) {
        qDebug() << "错误：UUID转换为二进制后为空，值为：" << uuid;
        return false;
    }

    m_recentMessageUuidKeys.insert(uk);

    MessageData msgData;
    msgData.sender = sender;
    msgData.receiver = receiver;
    msgData.messageType = messageType;
    msgData.message = message;
    msgData.priTimestamp = priTimestamp;
    msgData.uploadTimeStamp = uploadTimeStamp;
    msgData.uuid = messageIdBin;

    m_msgQueue->enqueue(msgData);

    qDebug() << "消息入队成功，当前队列待插入数:" << m_msgQueue->size();
    return true;
}


// 把聊天记录添加到本地数据库(等待服务器回发确认后)
void MainWindow::toAddMessageToDatabase(const QString &sender,const QString &receiver, const QString &messageType,
                                      const QString &message,  const QString& priTimestamp, const QString& uploadTimeStamp, const QString &uuid)
{
    QUuid id = QUuid::fromString(uuid);
    if (id.isNull()) {
        qDebug() << "错误：传入的uuid格式无效，值为：" << uuid;
        return;
    }

    QByteArray messageIdBin = id.toRfc4122();
    if (messageIdBin.isEmpty()) {
        qDebug() << "错误：UUID转换为二进制后为空，值为：" << uuid;
        return;
    }

    MessageData msgData;
    msgData.sender = sender;
    msgData.receiver = receiver;
    msgData.messageType = messageType;
    msgData.message = message;
    msgData.priTimestamp = priTimestamp;
    msgData.uploadTimeStamp = uploadTimeStamp;
    msgData.uuid = messageIdBin;

    m_messageHash.insert(uuid, msgData);
}

// 判断是否显示时间戳
bool MainWindow::printTimeOrNot(const QString& messageTime, const QString& preMessageTime, QString& result)
{
    const QDateTime messageTimestamp = parseFlexibleChatTimestamp(messageTime);
    if (!messageTimestamp.isValid()) {
        result.clear();
        return false;
    }
    if (preMessageTime.isEmpty() || preMessageTime == QLatin1String("notime")) {
        formatMessageTime(messageTimestamp, result);
        return !result.isEmpty();
    }
    const QDateTime preMessageTimestamp = parseFlexibleChatTimestamp(preMessageTime);
    if (!preMessageTimestamp.isValid()) {
        formatMessageTime(messageTimestamp, result);
        return !result.isEmpty();
    }
    const qint64 secondsDifference = qAbs(preMessageTimestamp.secsTo(messageTimestamp));
    if (secondsDifference < 180) {
        result.clear();
        return false;
    }
    formatMessageTime(messageTimestamp, result);
    return !result.isEmpty();
}

// 格式化时间
void MainWindow::formatMessageTime(const QDateTime& messageTimestamp, QString& result)
{
    QDateTime current = QDateTime::currentDateTime();
    QDate dateToday = current.date();
    QDate dateTimestamp = messageTimestamp.date();

    // 如果是今天
    if (dateToday == dateTimestamp) {
        QString timePart = messageTimestamp.time().toString("hh:mm");
        QString period = chineseDayPeriodForHour(messageTimestamp.time().hour());
        result = QString("        %1 %2").arg(period).arg(timePart);
    }
    // 如果是昨天
    else if (dateToday == dateTimestamp.addDays(1)) {
        QString timePart = messageTimestamp.time().toString("hh:mm");
        QString period = chineseDayPeriodForHour(messageTimestamp.time().hour());
        result = QString("        昨天 %1 %2").arg(period).arg(timePart);
    }
    // 早于昨天：聊天窗口带「月日或年月日 + 时段 + 时刻」；列表侧仍仅用日期（见 displayTimeComparison）
    else {
        QString timePart = messageTimestamp.time().toString("hh:mm");
        QString period = chineseDayPeriodForHour(messageTimestamp.time().hour());
        const QString dateStr = formatChineseChatDateOnly(dateTimestamp);
        result = QString("        %1 %2 %3").arg(dateStr).arg(period).arg(timePart);
    }
}

// 更新某人的消息列表信息
void MainWindow::updateTalkList(const QString& friendId)
{
    QStandardItem *item = nullptr;
    for (int row = 0; row < m_talkModel->rowCount(); ++row) {
        QStandardItem *itemTmp = m_talkModel->item(row);
        if (!itemTmp) {
            continue;
        }
        if (itemTmp->data(Qt::UserRole + 1).toString() == friendId) {
            item = itemTmp;
            break;
        }
    }
    if (!item) {
        return;
    }

    if (!m_talkCache.contains(friendId)) { // 缓存中无该好友数据，直接返回
        return;
    }

    QVariantMap& talkData = m_talkCache[friendId];
    int unread = talkData["unread"].toInt();       // 未读数
    QString latestMessage = talkData["latest_msg"].toString(); // 最新消息
    QString timestamp = talkData["timestamp"].toString();      // 时间戳

    if (timestamp.isEmpty()) {
        timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    }

    AccountInfo& it = AccountMessageManager::getInstance()->getInfo(friendId);
    item->setData(latestMessage, Qt::UserRole + 5);  // 最新消息
    item->setData(timestamp, Qt::UserRole + 6);      // 时间戳
    item->setData(unread, Qt::UserRole + 10);        // 未读数
    const QString displayName = it.name.isEmpty()
                                    ? (it.account.isEmpty() ? friendId : it.account)
                                    : it.name;
    if (!displayName.isEmpty())
        item->setText(displayName);
}

// 更新某人的消息列表信息 指定更新
void MainWindow::updateTalkListFree(const QString& friendId, const QString& latestMessage, const QString& timestamp)
{
    if (!m_talkCache.contains(friendId)) {
        return;
    }

    QVariantMap& talkData = m_talkCache[friendId];
    talkData["latest_msg"] = latestMessage; // 更新缓存中的最新消息
    QString finalTimestamp = timestamp.isEmpty()
                                 ? QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
                                 : timestamp;
    talkData["timestamp"] = finalTimestamp; // 更新缓存中的时间戳

    QStandardItem *item = nullptr;
    for (int row = 0; row < m_talkModel->rowCount(); ++row) {
        QStandardItem *itemTmp = m_talkModel->item(row);
        if (!itemTmp) {
            continue;
        }
        if (itemTmp->data(Qt::UserRole + 1).toString() == friendId) {
            item = itemTmp;
            break;
        }
    }
    if (!item) {
        return;
    }

    item->setData(talkData["latest_msg"].toString(), Qt::UserRole + 5);  // 最新消息
    item->setData(talkData["timestamp"].toString(), Qt::UserRole + 6);   // 时间戳
}

// 使某人的未读消息条数加一
int MainWindow::updateUnread(const QString& friendId)
{
    const QModelIndex cur = ui->list_talks->currentIndex();
    if (cur.isValid() && cur.data(Qt::UserRole + 1).toString() == friendId) {
        return 0;
    }

    if (!m_talkCache.contains(friendId)) {
        qDebug() << "缓存中未找到好友" << friendId << "的会话数据，无法更新未读数";
        return 0;
    }

    QVariantMap& talkData = m_talkCache[friendId];
    int unread = talkData["unread"].toInt(); // 从缓存读取当前未读数
    unread += 1;                             // 未读数+1
    talkData["unread"] = unread;             // 更新缓存中的未读数

    QStandardItem *item = nullptr;
    for (int row = 0; row < m_talkModel->rowCount(); ++row) {
        QStandardItem *itemTmp = m_talkModel->item(row);
        if (!itemTmp) {
            continue;
        }
        if (itemTmp->data(Qt::UserRole + 1).toString() == friendId) {
            item = itemTmp;
            break;
        }
    }
    if (!item) {
        return 0;
    }

    item->setData(unread, Qt::UserRole + 10);

    return unread;
}

// 清空某人的未读消息
void MainWindow::clearUnread(const QString& friendId)
{
    if(friendId.isEmpty()){
        return;
    }

    if (!m_talkCache.contains(friendId)) {
        qDebug() << "缓存中未找到好友" << friendId << "的会话数据，无法清空未读数";
        return;
    }

    QVariantMap& talkData = m_talkCache[friendId];
    talkData["unread"] = 0;

    QStandardItem *item = nullptr;
    for (int row = 0; row < m_talkModel->rowCount(); ++row) {
        QStandardItem *itemTmp = m_talkModel->item(row);
        if (!itemTmp) {
            continue;
        }
        if (itemTmp->data(Qt::UserRole + 1).toString() == friendId) {
            item = itemTmp;
            break;
        }
    }
    if (!item) {
        flushTalksCacheToDatabase();
        return;
    }
    item->setData(0, Qt::UserRole + 10);
    flushTalksCacheToDatabase();
}

void MainWindow::flushTalksCacheToDatabase()
{
    if (!m_db.isValid() || !m_db.isOpen()) {
        return;
    }

    if (!m_db.transaction()) {
        qDebug() << "[flushTalks] 事务开启失败" << m_db.lastError().text();
        return;
    }

    QSqlQuery qry(m_db);
    bool allSuccess = true;
    const QStringList keepIds = m_talkCache.keys();
    static const int kMaxBindParams = 500;

    if (!keepIds.isEmpty() && keepIds.size() <= kMaxBindParams) {
        QString placeholders;
        for (int i = 0; i < keepIds.size(); ++i) {
            if (i > 0) {
                placeholders += QLatin1Char(',');
            }
            placeholders += QLatin1Char('?');
        }
        qry.prepare(QStringLiteral("DELETE FROM talks WHERE friend_id NOT IN (%1)").arg(placeholders));
        for (int i = 0; i < keepIds.size(); ++i) {
            qry.bindValue(i, keepIds.at(i));
        }
        if (!qry.exec()) {
            qDebug() << "[flushTalks] 清理过期会话失败" << qry.lastError().text();
            m_db.rollback();
            return;
        }
    } else {
        if (!qry.exec(QStringLiteral("DELETE FROM talks"))) {
            qDebug() << "[flushTalks] 清空 talks 失败" << qry.lastError().text();
            m_db.rollback();
            return;
        }
    }

    for (auto it = m_talkCache.constBegin(); it != m_talkCache.constEnd(); ++it) {
        const QString& friendId = it.key();
        const QVariantMap& talkData = it.value();

        qry.prepare(QStringLiteral(
            "UPDATE talks SET unread=:unread, latest_msg=:latest_msg, timestamp=:timestamp WHERE friend_id=:friend"));
        qry.bindValue(QStringLiteral(":friend"), friendId);
        qry.bindValue(QStringLiteral(":unread"), talkData.value(QStringLiteral("unread")).toInt());
        qry.bindValue(QStringLiteral(":latest_msg"), talkData.value(QStringLiteral("latest_msg")).toString());
        qry.bindValue(QStringLiteral(":timestamp"), talkData.value(QStringLiteral("timestamp")).toString());

        if (!qry.exec()) {
            allSuccess = false;
            break;
        }
        if (qry.numRowsAffected() > 0) {
            continue;
        }

        qry.prepare(QStringLiteral(
            "INSERT INTO talks (friend_id, unread, latest_msg, timestamp) "
            "VALUES (:friend, :unread, :latest_msg, :timestamp)"));
        qry.bindValue(QStringLiteral(":friend"), friendId);
        qry.bindValue(QStringLiteral(":unread"), talkData.value(QStringLiteral("unread")).toInt());
        qry.bindValue(QStringLiteral(":latest_msg"), talkData.value(QStringLiteral("latest_msg")).toString());
        qry.bindValue(QStringLiteral(":timestamp"), talkData.value(QStringLiteral("timestamp")).toString());

        if (!qry.exec()) {
            allSuccess = false;
            break;
        }
    }

    if (allSuccess && m_db.commit()) {
        qDebug() << "[flushTalks] talks 已同步" << m_talkCache.size() << "条";
    } else {
        m_db.rollback();
        qDebug() << "[flushTalks] 同步失败，已回滚" << m_db.lastError().text();
    }
}

// 把某人从消息列表提升到最上面
void MainWindow::liftSomebody(const QString& friendId)
{
    // 空值/空模型直接返回（优先级最高）
    if (friendId.isEmpty() || m_talkModel->rowCount() == 0) {
        return;
    }

    // 移动行前记下当前选中的是哪个账号（按行号会错位：例如正看 C 时把 B 顶到第 0 行，原第 0 行变成 C 但列表当前行仍指向 0，会误显示成选了 B）
    const QModelIndex curTalkBefore = ui->list_talks->currentIndex();
    const QString prevSelectedAccount =
        curTalkBefore.isValid() ? curTalkBefore.data(Qt::UserRole + 1).toString() : QString();

    // 检查目标项是否已经在第0行（最顶部），是则直接返回
    QStandardItem *item0 = m_talkModel->item(0);
    if (item0 && item0->data(Qt::UserRole + 1).toString() == friendId) {
        return;
    }

    // 查找对应 friendId 的项
    QStandardItem *targetItem = nullptr;
    int targetRow = -1;
    for (int row = 0; row < m_talkModel->rowCount(); ++row) {
        QStandardItem *itemTmp = m_talkModel->item(row);
        if (itemTmp && itemTmp->data(Qt::UserRole + 1).toString() == friendId) {
            targetItem = itemTmp;
            targetRow = row;
            break;
        }
    }

    // 未找到目标项，直接返回
    if (!targetItem || targetRow == -1) {
        return;
    }

    QStandardItem *movedItem = m_talkModel->takeItem(targetRow);
    m_talkModel->insertRow(0, movedItem);

    // 按账号恢复选中，保证会话列表与 stack_talks 一致，避免顶起会话后误操作/野页面崩溃
    if (!prevSelectedAccount.isEmpty()) {
        selectSomeoneInTalkList(prevSelectedAccount);
    }
}

// 清空某人聊天记录缓存
void MainWindow::clearSomebodyTalkMessages(const QString& friendId)
{
    if (friendId.isEmpty()) {
        return;
    }

    QDir baseDir(m_apDir);
    QString friendCacheDir = baseDir.filePath(friendId);

    QDir targetDir(friendCacheDir);
    if (!targetDir.exists()) {
        return;
    }

    targetDir.removeRecursively();
}

void MainWindow::refreshMessageAvatarsForAccount(const QString& account)
{
    if (account.isEmpty()) return;
    AvatarManager::getInstance()->clearAvatarCache(account);
    m_lazyStaleMessageAvatarAccounts.insert(account);
    if (account == m_myInfo.account)
        m_lazySelfAvatarPagesRefreshed.clear();

    QWidget* cur = ui->stack_talks->currentWidget();
    if (cur && cur != m_emptyTalkPage && cur->objectName() != QLatin1String("EmptyTalkPage"))
        refreshLazyMessageAvatarsOnPage(cur);
}

void MainWindow::refreshLazyMessageAvatarsOnPage(QWidget* page)
{
    if (!page || m_lazyStaleMessageAvatarAccounts.isEmpty())
        return;
    if (page == m_emptyTalkPage || page->objectName() == QLatin1String("EmptyTalkPage"))
        return;

    QListWidget* list = page->findChild<QListWidget*>();
    if (!list)
        return;

    const QString peerAccount = page->objectName();

    for (int r = 0; r < list->count(); ++r) {
        QWidget* w = list->itemWidget(list->item(r));
        if (!w)
            continue;
        const QList<LabelFriendAvaInMessage*> labs = w->findChildren<LabelFriendAvaInMessage*>();
        for (LabelFriendAvaInMessage* lab : labs) {
            QString sender = lab->property("msgSenderAccount").toString();
            if (sender.isEmpty())
                sender = inferMessageSenderFromRow(w, peerAccount, m_myInfo.account);
            if (!m_lazyStaleMessageAvatarAccounts.contains(sender))
                continue;
            const QPixmap pix = AvatarManager::getInstance()->loadAvator(sender, QSize(40, 40));
            lab->setPixmap(pix);
        }
    }

    if (!peerAccount.isEmpty() && peerAccount != m_myInfo.account
        && m_lazyStaleMessageAvatarAccounts.contains(peerAccount)) {
        m_lazyStaleMessageAvatarAccounts.remove(peerAccount);
    }

    if (m_lazyStaleMessageAvatarAccounts.contains(m_myInfo.account)) {
        m_lazySelfAvatarPagesRefreshed.insert(page);
        if (m_lazySelfAvatarPagesRefreshed.size() >= m_talksItem.size()) {
            m_lazyStaleMessageAvatarAccounts.remove(m_myInfo.account);
            m_lazySelfAvatarPagesRefreshed.clear();
        }
    }
}

// 获取录制的音频时长
QString MainWindow::getAudioTime(const QByteArray& audioData)
{
    qint64 audioDataSize = audioData.size(); // 音频数据总字节数
    int bytesPerSample = m_format.bytesPerSample(); // 每个样本的字节数
    int channels = m_format.channelCount();         // 声道数
    int sampleRate = m_format.sampleRate();         // 采样率
    qint64 bytesPerSecond = static_cast<qint64>(sampleRate) * bytesPerSample * channels;
    if (bytesPerSecond <= 0) {
        return QStringLiteral("0s");
    }
    int sec = static_cast<int>(audioDataSize / bytesPerSecond);
    return sec < 60 ? QString("%1s").arg(sec) :
               sec < 3600 ? QString("%1m%2s").arg(sec/60).arg(sec%60) :
               QString("%1h%2m%3s").arg(sec/3600).arg((sec%3600)/60).arg(sec%60);
}
