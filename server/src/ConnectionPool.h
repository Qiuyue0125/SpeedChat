#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

/**
 * @file ConnectionPool.h
 * MySQL 连接池：每线程至多一条连接，与线程生命周期绑定。
 */

#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QDateTime>
#include <QThread>

// 数据库连接池：每 QThread 最多一条连接；连接与线程强绑定，
// 不按空闲定时关闭，仅在线程退出（removeThreadConnection）、连接失效或 clearConnection 时回收。
class ConnectionPool : public QObject
{
public:
    // 获取单例实例
    static ConnectionPool& getInstance();

    // 获取数据库连接
    QSqlDatabase getConnection(int timeoutMs = 3000);

    // 清空所有连接
    void clearConnection();

    // 释放连接
    void releaseConnection(QSqlDatabase& db);

    // 设置最大连接数
    void setMaxConnections(int max);

    // 获取最大连接数
    int getMaxConnections() const;

    // 移除线程连接
    void removeThreadConnection(QThread* thread);

    // 数据库是否初始化成功（驱动可用 + 表创建完成）
    bool isInitialized() const;
private:
    // 构造函数
    explicit ConnectionPool(QObject *parent = nullptr);

    // 析构函数
    ~ConnectionPool();

    // 禁用拷贝构造和赋值运算符
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // 初始化数据库
    bool initDatabase();

    // 检查连接有效性
    bool isConnectionValid(const QSqlDatabase& db);

    // 生成连接名
    QString generateConnectionName();

    // 确保工作线程上有 QObject 锚点，供跨线程投递 close/removeDatabase（必须在目标线程上调用 libmysql）。
    void ensureThreadCleanupAnchor(QThread *thread);
    // 在 ownerThread 上关闭并 removeDatabase；线程已退出则在当前线程清理。
    void dispatchCloseRemoveOnThread(QThread *ownerThread, const QString &connName);
    // 线程池回收线程后删除锚点（小 QObject，每线程一个）。
    void dropThreadAnchor(QThread *thread);

    // 线程 -> Qt 连接名（只存 QString，禁止在非所属线程持有/析构 QSqlDatabase，否则会触发 libmysql 断点）
    QHash<QThread*, QString> m_threadConnections;
    // 线程锁
    mutable QMutex m_mutex;
    // 连接可用条件变量（替代轮询等待）
    QWaitCondition m_connAvailable;
    // 最大连接数
    int m_maxConnections{100};
    // 初始化锁
    static QMutex m_initMutex;
    // MySQL 连接超时秒（MYSQL_OPT_CONNECT_TIMEOUT）
    int m_connConnectTimeoutSec = 15;

    // 数据库连接参数
    QString m_dbHost;
    QString m_dbName;
    QString m_dbUser;
    QString m_dbPassword;
    int m_dbPort = 0;
    // 连接健康检查最小间隔，避免每次借还都做 SELECT 1
    int m_connHealthCheckIntervalMs = 5000;
    // 连接上次健康检查时间
    QHash<QString, qint64> m_connLastHealthCheckMs;
    // 健康检查缓存独立锁，降低主连接锁竞争
    QMutex m_healthMutex;

    // 每工作线程一个 QObject，affinity=该线程，用于 BlockingQueuedConnection 安全清理 QMYSQL 连接
    mutable QMutex m_threadAnchorMutex;
    QHash<QThread*, QObject *> m_threadCleanupAnchors;

    // 数据库初始化是否成功（驱动可用 + 表创建完成）
    bool m_initialized = false;
};

// 数据库连接守卫类
// 析构时自动释放连接
class DbConnectionGuard {
public:
    // 构造函数
    explicit DbConnectionGuard(QSqlDatabase& db)
        : m_db(db), m_isReleased(false) {}

    // 析构函数
    ~DbConnectionGuard() {
        if (!m_isReleased) {
            ConnectionPool::getInstance().releaseConnection(m_db);
        }
    }

    // 禁用拷贝构造和赋值运算符
    DbConnectionGuard(const DbConnectionGuard&) = delete;
    DbConnectionGuard& operator=(const DbConnectionGuard&) = delete;

    // 手动释放连接
    void release() {
        if (!m_isReleased) {
            ConnectionPool::getInstance().releaseConnection(m_db);
            m_isReleased = true;
        }
    }

private:
    // 数据库连接引用
    QSqlDatabase& m_db;
    // 是否已释放
    bool m_isReleased;
};

#endif // 结束
