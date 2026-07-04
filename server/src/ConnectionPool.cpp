/**
 * @file ConnectionPool.cpp
 * MySQL 连接池：借还连接、配置项、进程内上限与线程安全。
 */
#include "ConnectionPool.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QSettings>
#include <QFile>
#include <QMetaObject>
#include <atomic>
#include <mutex>
#include "ServerConfigDefaults.h"

// 初始化静态互斥锁
QMutex ConnectionPool::m_initMutex;

// 释放无效连接日志限流：避免 MySQL 连接失败时刷屏
namespace {
static std::atomic<int> s_releaseInvalidCount{0};
static std::atomic<qint64> s_lastReleaseInvalidLogMs{0};
static constexpr int kReleaseInvalidLogIntervalMs = 5000;  // 每 5 秒最多打印 1 次
static constexpr int kReleaseInvalidLogSample = 50;        // 或每 50 次打印 1 次

// 在当前线程关闭并移除 Qt SQL 连接名（调用方必须处于可安全操作该连接的线程）。
void closeAndRemoveConnectionByName(const QString &connName)
{
    if (connName.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(connName, false);
        if (db.isValid() && db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connName);
}

// 所属工作线程已退出：禁止 QSqlDatabase::database()（跨线程会触发 libmysql EXCEPTION_BREAKPOINT）。
// 正常路径应已在线程存活时 close；此处仅注销 Qt 全局连接名。
void removeDatabaseNameOnly(const QString &connName)
{
    if (connName.isEmpty())
        return;
    QSqlDatabase::removeDatabase(connName);
}

// 将工作线程上的清理锚点 QObject 迁回应用主线程并销毁，避免泄漏。
// 线程仍运行：在对象所在线程 moveToThread(主线程) 后在主线程 delete（Qt 规则）。
// 线程已结束：不宜裸 moveToThread+delete（Qt6/MSVC 上可能 ACCESS_VIOLATION）；改为 Queued 到 QCoreApplication：
// setParent(连接池单例) 把亲和性迁到主线程，再 deleteLater，由事件循环析构。
void finalizeCleanupAnchorOnAppThread(QObject *anchor)
{
    if (!anchor)
        return;

    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        delete anchor;
        return;
    }

    QThread *const mainThread = app->thread();
    QThread *const objThread = anchor->thread();

    const auto deleteOnAppThread = [anchor]() { delete anchor; };

    const auto reparentAndDeleteLaterOnAppQueue = [anchor]() {
        ConnectionPool &pool = ConnectionPool::getInstance();
        anchor->setParent(&pool);
        anchor->deleteLater();
    };

    if (objThread == mainThread) {
        if (QThread::currentThread() == mainThread)
            delete anchor;
        else
            QMetaObject::invokeMethod(app, deleteOnAppThread, Qt::BlockingQueuedConnection);
        return;
    }

    if (!objThread || !objThread->isRunning()) {
        QMetaObject::invokeMethod(app, reparentAndDeleteLaterOnAppQueue, Qt::QueuedConnection);
        return;
    }

    const bool ok = QMetaObject::invokeMethod(
        anchor,
        [anchor, mainThread]() { anchor->moveToThread(mainThread); },
        Qt::BlockingQueuedConnection);
    if (!ok) {
        qCritical() << "【连接池】finalizeCleanupAnchor：moveToThread 投递失败，改用 parent+deleteLater";
        QMetaObject::invokeMethod(app, reparentAndDeleteLaterOnAppQueue, Qt::QueuedConnection);
        return;
    }

    if (QThread::currentThread() == mainThread)
        delete anchor;
    else
        QMetaObject::invokeMethod(app, deleteOnAppThread, Qt::BlockingQueuedConnection);
}
}

// 获取单例实例
ConnectionPool& ConnectionPool::getInstance()
{
    static ConnectionPool instance;
    return instance;
}

// 获取数据库连接
QSqlDatabase ConnectionPool::getConnection(int timeoutMs)
{
    QThread* currentThread = QThread::currentThread();
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // 驱动检查
    static std::once_flag s_mysqlDriverOnce;
    static bool s_hasMysqlDriver = false;
    std::call_once(s_mysqlDriverOnce, []() {
        s_hasMysqlDriver = QSqlDatabase::drivers().contains("QMYSQL");
        if (s_hasMysqlDriver) {
            qInfo() << "【连接池-初始化】MYSQL驱动检测成功，支持MYSQL连接";
        } else {
            qCritical() << "【连接池-错误】MYSQL驱动未加载！请检查驱动配置";
        }
    });
    if (!s_hasMysqlDriver) {
        return QSqlDatabase();
    }

    // 每工作线程一个锚点 QObject，供线程退出时把 close/removeDatabase 投递回本线程（libmysql 跨线程易崩）。
    ensureThreadCleanupAnchor(currentThread);

    while (true) {
        QMutexLocker locker(&m_mutex);

        // 检查当前线程已有连接
        if (m_threadConnections.contains(currentThread)) {
            const QString connName = m_threadConnections.value(currentThread);
            locker.unlock();
            QSqlDatabase db = QSqlDatabase::database(connName, false);
            if (!db.isValid()) {
                QMutexLocker relocker(&m_mutex);
                m_threadConnections.remove(currentThread);
                {
                    QMutexLocker healthLocker(&m_healthMutex);
                    m_connLastHealthCheckMs.remove(connName);
                }
                m_connAvailable.wakeOne();
                relocker.unlock();
                QSqlDatabase::removeDatabase(connName);
                continue;
            }
            bool isValid = isConnectionValid(db);
            locker.relock();

            if (isValid) {
                // 持锁后再次确认连接仍归属当前线程，避免理论上的竞态
                if (m_threadConnections.contains(currentThread) &&
                    m_threadConnections.value(currentThread) == connName) {
                    return db;
                }
                continue;  // 连接已易主或已被移除，重新循环
            } else {
                // 移除失效连接：必须先销毁 db 引用再 removeDatabase，否则 Qt 报 "connection is still in use"
                m_threadConnections.remove(currentThread);
                {
                    QMutexLocker healthLocker(&m_healthMutex);
                    m_connLastHealthCheckMs.remove(connName);
                }
                locker.unlock();
                db.close();
                db = QSqlDatabase();  // 销毁引用后再 removeDatabase
                QSqlDatabase::removeDatabase(connName);
                locker.relock();

                m_connAvailable.wakeOne();

                qWarning() << "【连接池-移除失效连接】线程地址：" << currentThread
                           << "，连接名：" << connName
                           << "，剩余总连接数：" << m_threadConnections.size();
                continue;  // 继续循环尝试获取连接
            }
        }

        // 尝试创建新连接
        if (m_threadConnections.size() < m_maxConnections) {
            QString connName = generateConnectionName();
            locker.unlock();

            bool openSuccess = false;
            QSqlDatabase db;
            for (int attempt = 0; attempt < 3 && !openSuccess; ++attempt) {
                if (attempt > 0) {
                    db = QSqlDatabase();
                    QSqlDatabase::removeDatabase(connName);
                    db = QSqlDatabase::addDatabase("QMYSQL", connName);
                } else {
                    db = QSqlDatabase::addDatabase("QMYSQL", connName);
                }
                db.setHostName(m_dbHost);
                db.setPort(m_dbPort);
                db.setDatabaseName(m_dbName);
                db.setUserName(m_dbUser);
                db.setPassword(m_dbPassword);
                db.setConnectOptions(QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;"
                                            "MYSQL_OPT_READ_TIMEOUT=30;"
                                            "MYSQL_OPT_WRITE_TIMEOUT=30").arg(m_connConnectTimeoutSec));
                openSuccess = db.open();
                if (openSuccess) {
                    // 关闭 autocommit，使 Qt 驱动能正确跟踪事务状态
                    QSqlQuery autoOff(db);
                    autoOff.exec("SET autocommit=0");
                } else {
                    db.close();
                    QSqlDatabase::removeDatabase(connName);
                    if (attempt < 2)
                        QThread::msleep(300);
                }
            }
            locker.relock();

            if (openSuccess) {
                m_threadConnections.insert(currentThread, connName);
                qInfo() << "【连接池-创建新连接】线程地址：" << currentThread
                        << "，连接名：" << connName
                        << "，当前总连接数：" << m_threadConnections.size()
                        << "，最大连接数：" << m_maxConnections;
                return db;
            } else {
                static std::atomic<int> s_createFailCount{0};
                const int failCnt = s_createFailCount.fetch_add(1) + 1;
                if (failCnt <= 3 || failCnt % 20 == 0) {
                    qCritical() << "【连接池-创建连接失败】累计" << failCnt << "次，错误：" << db.lastError().text();
                }
                db = QSqlDatabase();  // 已在循环内 close+removeDatabase
                return QSqlDatabase();
            }
        }

        // 连接池满时基于条件变量等待，避免 msleep 轮询
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
        if (elapsed >= timeoutMs) {
            qCritical() << "【连接池-获取连接超时】线程地址：" << currentThread
                        << "，等待时长：" << elapsed << "ms（超时阈值：" << timeoutMs << "ms）"
                        << "，当前总连接数：" << m_threadConnections.size()
                        << "，最大连接数：" << m_maxConnections;
            return QSqlDatabase();
        }
        const int remainMs = static_cast<int>(qMax<qint64>(1, timeoutMs - elapsed));
        if (!m_connAvailable.wait(&m_mutex, static_cast<unsigned long>(remainMs))) {
            qint64 totalElapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
            qCritical() << "【连接池-获取连接超时】线程地址：" << currentThread
                        << "，等待时长：" << totalElapsed << "ms（超时阈值：" << timeoutMs << "ms）"
                        << "，当前总连接数：" << m_threadConnections.size()
                        << "，最大连接数：" << m_maxConnections;
            return QSqlDatabase();
        }
    }
}

// 清空所有连接
void ConnectionPool::clearConnection()
{
    QList<QPair<QThread*, QString>> toClose;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_threadConnections.begin(); it != m_threadConnections.end(); ++it) {
            const QString &name = it.value();
            if (!name.isEmpty())
                toClose.append(qMakePair(it.key(), name));
        }
        m_threadConnections.clear();
    }

    for (const auto &p : toClose) {
        dispatchCloseRemoveOnThread(p.first, p.second);
        if (!p.second.isEmpty()) {
            QMutexLocker healthLocker(&m_healthMutex);
            m_connLastHealthCheckMs.remove(p.second);
        }
    }

    QMutexLocker locker(&m_mutex);
    m_connAvailable.wakeAll();
}

// 释放连接
void ConnectionPool::releaseConnection(QSqlDatabase& db)
{
    QMutexLocker locker(&m_mutex);
    QThread* currentThread = QThread::currentThread();

    if (!db.isValid()) {
        const int cnt = s_releaseInvalidCount.fetch_add(1) + 1;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastLog = s_lastReleaseInvalidLogMs.load();
        const bool shouldLog = (cnt <= 1) || (cnt % kReleaseInvalidLogSample == 0)
                               || (nowMs - lastLog >= kReleaseInvalidLogIntervalMs);

        // 连接已失效，但仍需从池中移除并释放槽位，否则会导致连接池泄漏
        if (m_threadConnections.contains(currentThread)) {
            const QString connName = m_threadConnections.take(currentThread);
            {
                QMutexLocker healthLocker(&m_healthMutex);
                m_connLastHealthCheckMs.remove(connName);
            }
            locker.unlock();
            closeAndRemoveConnectionByName(connName);
            locker.relock();
            m_connAvailable.wakeOne();
            if (shouldLog) {
                s_lastReleaseInvalidLogMs.store(nowMs);
                qWarning() << "【连接池】释放无效连接（已从池移除）累计" << cnt << "次，剩余连接数：" << m_threadConnections.size();
            }
        } else {
            if (shouldLog) {
                s_lastReleaseInvalidLogMs.store(nowMs);
                qWarning() << "【连接池】释放无效连接（未在池中，多为创建失败后释放）累计" << cnt << "次";
            }
        }
        return;
    }

    QString connName = db.connectionName();

    // 检查是否为当前线程有效连接
    if (m_threadConnections.contains(currentThread) &&
        m_threadConnections.value(currentThread) == connName) {
        locker.unlock();
        bool isValid = isConnectionValid(db);
        locker.relock();

        // 持锁后再次确认连接仍归属当前线程，避免理论上的竞态
        if (!m_threadConnections.contains(currentThread) ||
            m_threadConnections.value(currentThread) != connName) {
            return;  // 连接已易主或已被移除，无需处理
        }

        if (!isValid) {
            m_threadConnections.remove(currentThread);
            {
                QMutexLocker healthLocker(&m_healthMutex);
                m_connLastHealthCheckMs.remove(connName);
            }
            locker.unlock();
            db.close();
            db = QSqlDatabase();  // 销毁引用后再 removeDatabase
            QSqlDatabase::removeDatabase(connName);
            locker.relock();
            m_connAvailable.wakeOne();

            qWarning() << "【连接池-释放无效连接】线程地址：" << currentThread
                       << "，连接名：" << connName
                       << "，剩余总连接数：" << m_threadConnections.size();
        } else {
            // 连接与线程绑定：release 仅表示业务用完，连接仍留在本线程槽位，不按空闲定时关闭；
            // 仅在线程退出（removeThreadConnection）或连接失效时关闭。
            locker.unlock();
            // 归还前 COMMIT，关闭 SELECT 开启的隐式事务（autocommit=0），
            // 避免下次借用同一连接时使用过期快照。
            {
                QSqlQuery commitQry(db);
                commitQry.exec("COMMIT");
            }
            locker.relock();
            m_connAvailable.wakeOne();
        }
    } else {
        // 关闭非法连接：先销毁 db 引用再 removeDatabase
        locker.unlock();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
        locker.relock();
        {
            QMutexLocker healthLocker(&m_healthMutex);
            m_connLastHealthCheckMs.remove(connName);
        }
        m_connAvailable.wakeOne();

        qWarning() << "【连接池-强制关闭非法连接】线程地址：" << currentThread
                   << "，连接名：" << connName
                   << "（非当前线程绑定连接），剩余总连接数：" << m_threadConnections.size();
    }
}

// 设置最大连接数
void ConnectionPool::setMaxConnections(int max)
{
    if (max < 1) {
        qWarning() << "【连接池-警告】设置最大连接数失败：数值需≥1，传入值：" << max;
        return;
    }

    QMutexLocker locker(&m_mutex);
    m_maxConnections = max;
    m_connAvailable.wakeAll();
    locker.unlock();

    qInfo() << "【连接池-配置更新】连接池最大连接数已设置为：" << max;

    if (!ServerConfigDefaults::databasePoolAllowSetGlobalMaxConnections()) {
        qInfo() << "【连接池】跳过修改 MySQL 全局 max_connections（DatabasePool/AllowSetGlobalMaxConnections=false）";
        return;
    }

    QSqlDatabase db = getConnection();
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "【连接池-警告】获取数据库连接失败/连接未打开，无法修改MySQL全局max_connections";
        return;
    }
    DbConnectionGuard guard(db);

    QSqlQuery setQuery(db);
    setQuery.setForwardOnly(true);
    setQuery.prepare("SET GLOBAL max_connections = :max_conn");
    setQuery.bindValue(":max_conn", max);

    if (setQuery.exec()) {
        qInfo() << "【连接池-成功】MySQL全局max_connections已修改为：" << max;

        QSqlQuery checkQuery(db);
        checkQuery.setForwardOnly(true);
        if (checkQuery.exec("SHOW VARIABLES LIKE 'max_connections'")) {
            if (checkQuery.next()) {
                int actualMax = checkQuery.value(1).toInt();
                if (actualMax != max) {
                    qWarning() << "【连接池-警告】MySQL最大连接数修改后值不匹配，实际为：" << actualMax << "，期望：" << max;
                }
            }
        }
    } else {
        QSqlError err = setQuery.lastError();
        if (err.text().contains("SUPER privilege", Qt::CaseInsensitive)) {
            qWarning() << "【连接池-警告】修改MySQL最大连接数失败：当前用户无SUPER权限（仅影响MySQL全局配置，连接池上限已生效）";
        } else {
            qCritical() << "【连接池-错误】修改MySQL最大连接数失败：" << err.text();
        }
    }
}

// 获取最大连接数
int ConnectionPool::getMaxConnections() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxConnections;
}

// 移除线程连接
void ConnectionPool::removeThreadConnection(QThread* thread)
{
    if (!thread)
        return;

    QString connName;
    bool hadConn = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_threadConnections.contains(thread)) {
            hadConn = true;
            connName = m_threadConnections.take(thread);
            {
                QMutexLocker healthLocker(&m_healthMutex);
                m_connLastHealthCheckMs.remove(connName);
            }
            m_connAvailable.wakeOne();
        }
    }

    if (hadConn)
        dispatchCloseRemoveOnThread(thread, connName);

    // 线程池销毁 QThread 后不再复用该指针，可摘掉锚点
    dropThreadAnchor(thread);
}

void ConnectionPool::ensureThreadCleanupAnchor(QThread *thread)
{
    if (!thread || thread != QThread::currentThread())
        return;

    QMutexLocker anchorLocker(&m_threadAnchorMutex);
    if (m_threadCleanupAnchors.contains(thread))
        return;

    QObject *anchor = new QObject();
    anchor->moveToThread(thread);
    m_threadCleanupAnchors.insert(thread, anchor);
}

void ConnectionPool::dispatchCloseRemoveOnThread(QThread *ownerThread, const QString &connName)
{
    if (connName.isEmpty())
        return;

    if (!ownerThread || !ownerThread->isRunning()) {
        removeDatabaseNameOnly(connName);
        return;
    }

    if (QThread::currentThread() == ownerThread) {
        closeAndRemoveConnectionByName(connName);
        return;
    }

    QObject *anchor = nullptr;
    {
        QMutexLocker anchorLocker(&m_threadAnchorMutex);
        anchor = m_threadCleanupAnchors.value(ownerThread, nullptr);
    }

    if (!anchor) {
        qWarning() << "【连接池-警告】无清理锚点，仅 removeDatabase(连接名)（避免跨线程 database），连接名：" << connName;
        removeDatabaseNameOnly(connName);
        return;
    }

    const bool ok = QMetaObject::invokeMethod(
        anchor,
        [connName]() { closeAndRemoveConnectionByName(connName); },
        Qt::BlockingQueuedConnection);
    if (!ok) {
        qWarning() << "【连接池-警告】invokeMethod(清理连接)失败，仅 removeDatabase(连接名)：" << connName;
        removeDatabaseNameOnly(connName);
    }
}

void ConnectionPool::dropThreadAnchor(QThread *thread)
{
    if (!thread)
        return;

    QObject *anchor = nullptr;
    {
        QMutexLocker anchorLocker(&m_threadAnchorMutex);
        anchor = m_threadCleanupAnchors.take(thread);
    }
    if (!anchor)
        return;

    finalizeCleanupAnchorOnAppThread(anchor);
}

// 构造函数
ConnectionPool::ConnectionPool(QObject *parent)
    : QObject(parent)
{
    QMutexLocker locker(&m_initMutex);

    QString iniPath = ServerConfigDefaults::settingsIniPath();
    if (QFile::exists(iniPath)) {
        ServerConfigDefaults::ensureServerDefaults();
        QSettings cfg(iniPath, QSettings::IniFormat);

        cfg.beginGroup("Database");
        QString h = cfg.value("Host").toString();
        if (!h.isEmpty()) m_dbHost = h;
        int port = cfg.value("Port").toInt();
        if (port > 0) m_dbPort = port; else m_dbPort = ServerConfigDefaults::defaultDbPort();
        QString db = cfg.value("Database").toString();
        if (!db.isEmpty()) m_dbName = db;
        m_dbUser = cfg.value("User").toString();
        if (m_dbUser.isEmpty()) m_dbUser = ServerConfigDefaults::defaultDbUser();
        m_dbPassword = cfg.value("Password").toString();
        if (m_dbPassword.isEmpty()) m_dbPassword = ServerConfigDefaults::defaultDbPassword();
        cfg.endGroup();
        if (m_dbHost.isEmpty()) m_dbHost = ServerConfigDefaults::defaultDbHost();
        if (m_dbName.isEmpty()) m_dbName = ServerConfigDefaults::defaultDbName();

        cfg.beginGroup("DatabasePool");
        m_maxConnections = cfg.value("MaxConnections", ServerConfigDefaults::defaultMaxDbConnections()).toInt();
        m_connConnectTimeoutSec = cfg.value("ConnConnectTimeoutSec",
                                            ServerConfigDefaults::defaultDatabasePoolConnConnectTimeoutSec())
                                     .toInt();
        cfg.endGroup();
    }
    if (m_maxConnections < 1) m_maxConnections = ServerConfigDefaults::defaultMaxDbConnections();
    if (m_connConnectTimeoutSec < 5) {
        m_connConnectTimeoutSec = ServerConfigDefaults::defaultDatabasePoolConnConnectTimeoutSec();
    }
    if (m_dbPort <= 0) m_dbPort = ServerConfigDefaults::defaultDbPort();
    if (m_dbHost.isEmpty()) m_dbHost = ServerConfigDefaults::defaultDbHost();
    if (m_dbName.isEmpty()) m_dbName = ServerConfigDefaults::defaultDbName();
    if (m_dbUser.isEmpty()) m_dbUser = ServerConfigDefaults::defaultDbUser();
    if (m_dbPassword.isEmpty()) m_dbPassword = ServerConfigDefaults::defaultDbPassword();

    qInfo() << "【连接池-初始化】连接与线程强绑定：不按空闲定时关闭 MySQL 连接，仅在线程退出、连接失效或 clearConnection 时回收";

    // 初始化数据库
    m_initialized = initDatabase();
    if (!m_initialized) {
        qCritical() << "【连接池-初始化失败】数据库库/表创建失败，请检查数据库配置。服务器将无法正常处理数据库请求。";
    } else {
        qInfo() << "【连接池-初始化成功】数据库库/表创建完成，连接参数："
                << "主机：" << m_dbHost << "，端口：" << m_dbPort
                << "，数据库名：" << m_dbName << "，用户名：" << m_dbUser;
    }
}

// 数据库是否初始化成功（驱动可用 + 表创建完成）
bool ConnectionPool::isInitialized() const
{
    return m_initialized;
}

// 析构函数
ConnectionPool::~ConnectionPool()
{
    clearConnection();
}

// 初始化数据库
bool ConnectionPool::initDatabase()
{
    // 清空数据库仅用于开发环境
    QMutexLocker locker(&m_mutex);
    QStringList drivers = QSqlDatabase::drivers();

    // 检查驱动：打印所有可用 SQL 驱动和 Qt 插件搜索路径
    qInfo() << "【连接池-诊断】可用 SQL 驱动列表：" << drivers.join(", ");
    {
        const QStringList libPaths = QCoreApplication::libraryPaths();
        qInfo() << "【连接池-诊断】Qt 插件搜索路径：" << libPaths.join(" ; ");
    }
    if (!drivers.contains("QMYSQL")) {
        qCritical() << "【连接池-初始化错误】未检测到MYSQL驱动！可用驱动：" << drivers.join(", ");
        qCritical() << "【连接池-诊断】请将 qsqlmysql.so 及其依赖放置到 sqldrivers/ 目录。";
        return false;
    }

    bool initSuccess = false;
    // 生成初始化连接名
    QString initConnName = "init_connection_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    {
        QSqlDatabase initDb = QSqlDatabase::addDatabase("QMYSQL", initConnName);
        initDb.setHostName(m_dbHost);
        initDb.setPort(m_dbPort);
        initDb.setUserName(m_dbUser);
        initDb.setPassword(m_dbPassword);

        locker.unlock();
        // 连接数据库服务
        if (!initDb.open()) {
            qCritical() << "【连接池-初始化错误】连接MySQL服务器失败：" << initDb.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }
        {
            QSqlQuery autoOff(initDb);
            autoOff.exec("SET autocommit=0");
        }

        // 检查并创建数据库
        QSqlQuery checkDb(initDb);
        if (checkDb.exec(QString("SHOW DATABASES LIKE '%1'").arg(m_dbName)) && !checkDb.next()) {
            if (!checkDb.exec(QString("CREATE DATABASE IF NOT EXISTS %1 "
                                      "DEFAULT CHARACTER SET utf8mb4 "
                                      "DEFAULT COLLATE utf8mb4_unicode_ci").arg(m_dbName))) {
                qCritical() << "【连接池-初始化错误】创建数据库失败：" << checkDb.lastError().text();
                initDb.close();
                initDb = QSqlDatabase();
                QSqlDatabase::removeDatabase(initConnName);
                return false;
            }
            qInfo() << "【连接池-初始化】数据库" << m_dbName << "不存在，已创建";
        }

        // 打开目标数据库
        initDb.close();
        initDb.setDatabaseName(m_dbName);
        if (!initDb.open()) {
            qCritical() << "【连接池-初始化错误】打开数据库" << m_dbName << "失败：" << initDb.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        // 创建业务表
        QSqlQuery query(initDb);
        // 创建用户表
        QString createUserTable =
            "CREATE TABLE IF NOT EXISTS Users ("
            "account_number VARCHAR(20) PRIMARY KEY NOT NULL, "
            "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
            "password VARCHAR(255) NOT NULL, "
            "password_salt VARCHAR(32) NOT NULL DEFAULT '', "
            "avator LONGTEXT, "
            "nickname VARCHAR(50), "
            "signature TEXT, "
            "gender ENUM('男', '女', '其他'), "
            "question VARCHAR(255), "
            "answer VARCHAR(255), "
            "answer_salt VARCHAR(32) NOT NULL DEFAULT '' "
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
        if (!query.exec(createUserTable)) {
            qCritical() << "【连接池-初始化错误】创建Users表失败：" << query.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        // 创建消息表
        QString createMessageTable =
            "CREATE TABLE IF NOT EXISTS Messages ("
            "message_id BINARY(16) NOT NULL PRIMARY KEY, "
            "sender_id VARCHAR(20) NOT NULL, "
            "receiver_id VARCHAR(20) NOT NULL, "
            "content LONGTEXT, "
            "filename VARCHAR(255), "
            "status VARCHAR(20), "
            "timestamp DATETIME(3), "
            "message_type VARCHAR(20) NOT NULL, "
            "INDEX idx_sender_receiver_ts (sender_id, receiver_id, timestamp), "
            "INDEX idx_receiver_sender_ts (receiver_id, sender_id, timestamp), "
            "INDEX idx_receiver_status_ts (receiver_id, status, timestamp), "
            "FOREIGN KEY (sender_id) REFERENCES Users(account_number) ON DELETE CASCADE, "
            "FOREIGN KEY (receiver_id) REFERENCES Users(account_number) ON DELETE CASCADE"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
        if (!query.exec(createMessageTable)) {
            qCritical() << "【连接池-初始化错误】创建Messages表失败：" << query.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        //3. 创建好友表
        QString createFriendTable =
            "CREATE TABLE IF NOT EXISTS Friends ("
            "friendship_id INT AUTO_INCREMENT PRIMARY KEY, "
            "user_id VARCHAR(20) NOT NULL, "
            "friend_id VARCHAR(20) NOT NULL, "
            "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
            "INDEX idx_user (user_id), "
            "INDEX idx_friend (friend_id), "
            "UNIQUE KEY unique_friendship (user_id, friend_id), "
            "FOREIGN KEY (user_id) REFERENCES Users(account_number) ON DELETE CASCADE, "
            "FOREIGN KEY (friend_id) REFERENCES Users(account_number) ON DELETE CASCADE"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
        if (!query.exec(createFriendTable)) {
            qCritical() << "【连接池-初始化错误】创建Friends表失败：" << query.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        //4. 创建申请表
        QString createRequestTable =
            "CREATE TABLE IF NOT EXISTS FriendRequests ("
            "request_id INT AUTO_INCREMENT PRIMARY KEY, "
            "sender_id VARCHAR(20) NOT NULL, "
            "receiver_id VARCHAR(20) NOT NULL, "
            "request_type ENUM('friend', 'group') NOT NULL, "
            "status ENUM('pending', 'accepted', 'rejected') NOT NULL DEFAULT 'pending', "
            "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
            "INDEX idx_sender (sender_id), "
            "INDEX idx_receiver_status (receiver_id, status), "
            "INDEX idx_receiver_status_ts (receiver_id, status, timestamp), "
            "FOREIGN KEY (sender_id) REFERENCES Users(account_number) ON DELETE CASCADE, "
            "FOREIGN KEY (receiver_id) REFERENCES Users(account_number) ON DELETE CASCADE"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
        if (!query.exec(createRequestTable)) {
            qCritical() << "【连接池-初始化错误】创建FriendRequests表失败：" << query.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        // 注：idx_receiver_status_ts 已在 CREATE TABLE 的 INDEX 中定义，无需重复创建。

        // 速聊网盘：file_id 为上传 UUID；content 存 storage 相对路径（如 cloud/<uuid>.ext）
        const QString createCloudFilesTable =
            "CREATE TABLE IF NOT EXISTS CloudFiles ("
            "file_id BINARY(16) NOT NULL PRIMARY KEY, "
            "owner_id VARCHAR(20) NOT NULL, "
            "filename VARCHAR(255) NOT NULL, "
            "content VARCHAR(512) NOT NULL, "
            "file_size BIGINT NOT NULL DEFAULT 0, "
            "timestamp DATETIME(3) NOT NULL, "
            "INDEX idx_owner (owner_id), "
            "FOREIGN KEY (owner_id) REFERENCES Users(account_number) ON DELETE CASCADE"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
        if (!query.exec(createCloudFilesTable)) {
            qCritical() << "【连接池-初始化错误】创建CloudFiles表失败：" << query.lastError().text();
            initDb.close();
            initDb = QSqlDatabase();
            QSqlDatabase::removeDatabase(initConnName);
            return false;
        }

        initSuccess = true;
        initDb.close();
    }
    QSqlDatabase::removeDatabase(initConnName);

    return initSuccess;
}

// 检查连接有效性
bool ConnectionPool::isConnectionValid(const QSqlDatabase& db)
{
    if (!db.isOpen()) {
        return false;
    }

    const QString connName = db.connectionName();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 lastCheckMs = 0;
    {
        QMutexLocker healthLocker(&m_healthMutex);
        lastCheckMs = m_connLastHealthCheckMs.value(connName, 0);
    }
    if (lastCheckMs > 0 && (nowMs - lastCheckMs) < m_connHealthCheckIntervalMs) {
        return true;
    }

    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.exec("SELECT 1")) {
        qWarning() << "【连接池-连接无效】执行SELECT 1失败，连接名：" << connName
                   << "，错误信息：" << query.lastError().text();
        return false;
    }
    {
        QMutexLocker healthLocker(&m_healthMutex);
        m_connLastHealthCheckMs[connName] = nowMs;
    }
    return true;
}

// 生成连接名
QString ConnectionPool::generateConnectionName()
{
    quintptr threadAddr = reinterpret_cast<quintptr>(QThread::currentThread());
    return QString("Connection_%1_%2")
        .arg(threadAddr, 0, 16)
        .arg(QDateTime::currentMSecsSinceEpoch() % 10000);
}
