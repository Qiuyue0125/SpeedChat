#ifndef DBWORKER_H
#define DBWORKER_H

/**
 * @file DbWorker.h
 * 子线程执行 SQLite 访问，向主线程发信号回传结果。
 */

#include <QString>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QQueue>
#include <QThread>
#include <QSqlDatabase>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantMap>
#include <QStringList>
#include <QList>
#include <QHash>

// 消息数据
struct MessageData {
    QString sender;
    QString receiver;
    QString messageType;
    QString message;
    QString priTimestamp;
    QString uploadTimeStamp;
    QByteArray uuid;
};

// 聊天列表数据
struct TalksData {
    QHash<QString, QVariantMap> talkCache;
};

// 线程安全队列
template <typename T>
class ThreadSafeQueue {
public:
    // 构造函数
    ThreadSafeQueue() = default;
    // 析构函数
    ~ThreadSafeQueue() = default;

    // 入队（锁顺序：m_externalMutex < m_mutex，与 DbWorkerThread 的 wait 一致）
    void enqueue(const T& item) {
        if (m_externalMutex) {
            QMutexLocker extLocker(m_externalMutex);
            QMutexLocker locker(&m_mutex);
            m_queue.enqueue(item);
            m_condition.wakeOne();
            if (m_externalCondition) {
                m_externalCondition->wakeOne();
            }
        } else {
            QMutexLocker locker(&m_mutex);
            m_queue.enqueue(item);
            m_condition.wakeOne();
            if (m_externalCondition) {
                m_externalCondition->wakeOne();
            }
        }
    }

    // 出队
    T dequeue() {
        QMutexLocker locker(&m_mutex);
        while (m_queue.isEmpty()) {
            m_condition.wait(&m_mutex);
        }
        return m_queue.dequeue();
    }

    // 尝试出队
    bool tryDequeue(T& item) {
        QMutexLocker locker(&m_mutex);
        if (m_queue.isEmpty()) return false;
        item = m_queue.dequeue();
        return true;
    }

    // 判断为空
    bool isEmpty() const {
        QMutexLocker locker(&m_mutex);
        return m_queue.isEmpty();
    }

    // 获取数量
    int size() const {
        QMutexLocker locker(&m_mutex);
        return m_queue.size();
    }

    // 清空队列
    void clear() {
        QMutexLocker locker(&m_mutex);
        m_queue.clear();
    }

    // 额外唤醒条件（用于唤醒等待“任意队列有活”的工作线程）
    // 必须与 setExternalMutex 配对使用，锁顺序：m_externalMutex < m_mutex，避免死锁。
    void setExternalCondition(QWaitCondition *condition) {
        QMutexLocker locker(&m_mutex);
        m_externalCondition = condition;
    }

    // 设置外部互斥锁：enqueue 时在持有此锁的前提下 signal，确保与 waiter 的检查-等待原子性，避免 lost wakeup。
    void setExternalMutex(QMutex *mutex) {
        QMutexLocker locker(&m_mutex);
        m_externalMutex = mutex;
    }

private:
    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<T> m_queue;
    QWaitCondition *m_externalCondition = nullptr;
    QMutex *m_externalMutex = nullptr;
};

// 数据库线程
class DbWorkerThread : public QThread {
    Q_OBJECT
public:
    // 构造函数
    explicit DbWorkerThread(ThreadSafeQueue<MessageData>* msgQueue,
                            ThreadSafeQueue<TalksData>* talksQueue = nullptr,
                            const QString& dbName = "",
                            QObject *parent = nullptr);

    // 析构函数
    ~DbWorkerThread() override;

    // 停止线程
    void stop();

protected:
    // 线程入口
    void run() override;

private:
    // 插入消息
    void insertMessageToDb(QSqlDatabase& db, const MessageData& msgData);

    // 更新聊天列表
    void updateTalksToDb(QSqlDatabase& db, const TalksData& talksData);

private:
    ThreadSafeQueue<MessageData>* m_msgQueue;
    ThreadSafeQueue<TalksData>* m_talksQueue;
    QString m_dbFileName;
    bool m_stopFlag;
    QMutex m_stopMutex;
    QMutex m_waitMutex;
    QWaitCondition m_condition;
    QString m_connName;
};

#endif // 结束
