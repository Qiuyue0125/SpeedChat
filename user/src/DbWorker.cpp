/**
 * @file DbWorker.cpp
 * 后台写库线程：从队列消费 MessageData、写入 SQLite。
 */
#include "DbWorker.h"

// 构造函数
DbWorkerThread::DbWorkerThread(ThreadSafeQueue<MessageData>* msgQueue,
                               ThreadSafeQueue<TalksData>* talksQueue,
                               const QString& dbName,
                               QObject *parent)
    : QThread(parent)
    , m_msgQueue(msgQueue)
    , m_talksQueue(talksQueue)
    , m_dbFileName(dbName)
    , m_stopFlag(false)
{
    m_connName = "WorkerDB_" + QString::number((quint64)this);
    if (m_msgQueue) {
        m_msgQueue->setExternalMutex(&m_waitMutex);
        m_msgQueue->setExternalCondition(&m_condition);
    }
    if (m_talksQueue) {
        m_talksQueue->setExternalMutex(&m_waitMutex);
        m_talksQueue->setExternalCondition(&m_condition);
    }
}

// 析构函数
DbWorkerThread::~DbWorkerThread()
{
    stop();
    wait();
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::removeDatabase(m_connName);
        qDebug() << "移除数据库连接" << m_connName;
    }
}

// 停止线程
void DbWorkerThread::stop()
{
    QMutexLocker locker(&m_stopMutex);
    m_stopFlag = true;
    m_condition.wakeOne();
}

// 线程入口
void DbWorkerThread::run()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connName);
    db.setDatabaseName(m_dbFileName);
    if (!db.open()) {
        qDebug() << "工作线程数据库连接失败" << db.lastError().text();
        return;
    }

    while (true) {
        bool hasProcessed = false;

        MessageData msgData;
        if (m_msgQueue->tryDequeue(msgData)) {
            insertMessageToDb(db, msgData);
            hasProcessed = true;
        }

        if (m_talksQueue) {
            TalksData talksData;
            if (m_talksQueue->tryDequeue(talksData)) {
                qDebug() << "刷新聊天列表库中";
                updateTalksToDb(db, talksData);
                hasProcessed = true;
            }
        }

        {
            QMutexLocker locker(&m_stopMutex);
            bool isMsgQueueEmpty = m_msgQueue->isEmpty();
            bool isTalksQueueEmpty = m_talksQueue ? m_talksQueue->isEmpty() : true;
            if (m_stopFlag && isMsgQueueEmpty && isTalksQueueEmpty) {
                qDebug() << "线程退出 所有队列已排空";
                break;
            }
        }

        if (!hasProcessed) {
            QMutexLocker locker(&m_waitMutex);
            // 持 m_waitMutex 检查并等待，与 enqueue 的锁顺序一致，避免 lost wakeup
            const bool isMsgQueueEmpty = m_msgQueue->isEmpty();
            const bool isTalksQueueEmpty = m_talksQueue ? m_talksQueue->isEmpty() : true;
            if (isMsgQueueEmpty && isTalksQueueEmpty) {
                m_condition.wait(&m_waitMutex);
            }
        }
    }

    if (db.isOpen()) {
        db.close();
    }
}

// 插入消息
void DbWorkerThread::insertMessageToDb(QSqlDatabase& db, const MessageData& msgData)
{
    QString processedServerTime = msgData.priTimestamp;
    QString processedReceivedTime = msgData.uploadTimeStamp;

    if (msgData.uuid.isEmpty()) {
        qDebug() << "错误 消息ID为空 拒绝插入";
        return;
    }

    QSqlQuery qry(db);
    qry.prepare(R"(
        INSERT OR IGNORE INTO messages (message_id, sender, receiver, messagetype, message, server_timestamp, received_timestamp)
        VALUES (:message_id, :sender, :receiver, :messagetype, :message, :server_timestamp, :received_timestamp)
    )");

    qry.bindValue(":message_id", msgData.uuid);
    qry.bindValue(":sender", msgData.sender);
    qry.bindValue(":receiver", msgData.receiver);
    qry.bindValue(":messagetype", msgData.messageType);
    qry.bindValue(":message", msgData.message);
    qry.bindValue(":server_timestamp", processedServerTime);
    qry.bindValue(":received_timestamp", processedReceivedTime);

    if (!qry.exec()) {
        qDebug() << "异步插入消息失败" << qry.lastError().text();
    }
}

// 更新聊天列表
void DbWorkerThread::updateTalksToDb(QSqlDatabase& db, const TalksData& talksData)
{
    if (!db.transaction()) {
        qDebug() << "事务开启失败" << db.lastError().text();
        return;
    }

    QSqlQuery qry(db);
    bool allSuccess = true;

    // 1. 删除已不在 talkCache 中的会话（增量：仅移除多余项）
    // 注：SQLite 绑定参数上限约 999，好友数过多时回退全表删除
    const QStringList keepIds = talksData.talkCache.keys();
    const int kMaxBindParams = 500;  // 保守值，避免 NOT IN (?) 超限
    if (!keepIds.isEmpty() && keepIds.size() <= kMaxBindParams) {
        QString placeholders;
        for (int i = 0; i < keepIds.size(); ++i) {
            if (i > 0) placeholders += ",";
            placeholders += "?";
        }
        qry.prepare("DELETE FROM talks WHERE friend_id NOT IN (" + placeholders + ")");
        for (int i = 0; i < keepIds.size(); ++i) {
            qry.bindValue(i, keepIds.at(i));
        }
        if (!qry.exec()) {
            qDebug() << "清理过期会话失败 事务回滚" << qry.lastError().text();
            db.rollback();
            return;
        }
    } else {
        qry.exec("DELETE FROM talks");
        if (qry.lastError().isValid()) {
            qDebug() << "清空表失败 事务回滚" << qry.lastError().text();
            db.rollback();
            return;
        }
    }

    // 2. 按 friend_id 更新或插入（UPDATE 成功则跳过 INSERT，减少写操作）
    foreach (const QString& friendId, talksData.talkCache.keys()) {
        const QVariantMap& talkData = talksData.talkCache[friendId];

        qry.prepare("UPDATE talks SET unread=:unread, latest_msg=:latest_msg, timestamp=:timestamp "
                    "WHERE friend_id=:friend");
        qry.bindValue(":friend", friendId);
        qry.bindValue(":unread", talkData["unread"].toInt());
        qry.bindValue(":latest_msg", talkData["latest_msg"].toString());
        qry.bindValue(":timestamp", talkData["timestamp"].toString());

        if (!qry.exec()) {
            allSuccess = false;
            break;
        }
        if (qry.numRowsAffected() > 0) continue;

        qry.prepare("INSERT INTO talks (friend_id, unread, latest_msg, timestamp) "
                    "VALUES (:friend, :unread, :latest_msg, :timestamp)");
        qry.bindValue(":friend", friendId);
        qry.bindValue(":unread", talkData["unread"].toInt());
        qry.bindValue(":latest_msg", talkData["latest_msg"].toString());
        qry.bindValue(":timestamp", talkData["timestamp"].toString());

        if (!qry.exec()) {
            allSuccess = false;
            break;
        }
    }

    if (allSuccess && db.commit()) {
        qDebug() << "数据更新成功 增量更新" << talksData.talkCache.size() << "条记录";
    } else {
        db.rollback();
        qDebug() << "数据更新失败 事务已回滚";
    }
}

