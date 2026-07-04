/**
 * @file ThreadPool.cpp
 * 业务任务线程池：创建/回收工作线程、优雅停止、默认配置。
 */
#include "ThreadPool.h"
#include "ConnectionPool.h"
#include <QCoreApplication>
#include <QTimer>
#include <QMetaObject>

namespace {
// 尽量平滑停止线程：请求中断 + quit + 等待（不使用 terminate）。
// 返回是否在 waitMs 内完成退出。
bool stopThreadGracefully(QThread *thread, int waitMs)
{
    if (!thread) return false;
    thread->requestInterruption();
    thread->quit();
    return thread->wait(static_cast<unsigned long>(waitMs));
}
}

// 构造线程池并初始化默认参数。
ThreadPool::ThreadPool(QObject *parent) : QObject(parent)
{
    ServerConfigDefaults::ensureServerDefaults();
    // 自 Settings.ini [ThreadPool] 读取 MaxThreads、IdleTimeoutMs、CheckIntervalMs（经 ServerConfigDefaults 缓存）。
    MAX_THREAD_COUNT = ServerConfigDefaults::threadPoolMaxThreads();
    IDLE_THREAD_TIMEOUT = ServerConfigDefaults::threadPoolIdleTimeoutMs();
    DB_CHECK_INTERVAL = ServerConfigDefaults::threadPoolCheckIntervalMs();

    m_idleCheckTimer = new QTimer(this);
    m_idleCheckTimer->setInterval(DB_CHECK_INTERVAL);
    connect(m_idleCheckTimer, &QTimer::timeout, this, &ThreadPool::checkAndReleaseIdleThreads);
    m_idleCheckTimer->start();

    qInfo() << "【线程池初始化】最大线程数：" << MAX_THREAD_COUNT
            << "，空闲超时：" << IDLE_THREAD_TIMEOUT/1000 << "秒，检查间隔：" << DB_CHECK_INTERVAL/1000 << "秒";
}

// 析构线程池并回收全部线程。
ThreadPool::~ThreadPool()
{
    clear();
}

// 返回线程池单例实例。
ThreadPool& ThreadPool::getInstance()
{
    static ThreadPool instance;
    return instance;
}

// 获取一个可用工作线程（复用或新建）
QThread* ThreadPool::getThread()
{
    QMutexLocker locker(&m_mutex);

    if (!m_idleThreads.isEmpty()) {
        QThread* thread = m_idleThreads.takeFirst();
        m_workingThreads.append(thread);
        m_idleThreadTimestamps.remove(thread);
        return thread;
    }

    if (m_allThreads.size() < MAX_THREAD_COUNT) {
        QThread* newThread = createNewThread();
        m_workingThreads.append(newThread);
        m_allThreads.append(newThread);
        return newThread;
    }

    qWarning() << "【线程池警告】线程池已达上限（" << MAX_THREAD_COUNT << "），无法创建新线程"
               << "，当前工作线程数：" << m_workingThreads.size();
    return nullptr;
}

// 释放线程回线程池空闲列表
void ThreadPool::releaseThread(QThread* thread)
{
    if (!thread) return;

    QMutexLocker locker(&m_mutex);
    if (m_workingThreads.removeOne(thread)) {
        m_idleThreads.append(thread);
        m_idleThreadTimestamps[thread] = QDateTime::currentMSecsSinceEpoch();
        // 接近满载时记录释放，便于确认线程能正常回收
        if (m_workingThreads.size() >= MAX_THREAD_COUNT * 9 / 10) {
            qInfo() << "【线程池-释放】工作:" << m_workingThreads.size()
                    << "空闲:" << m_idleThreads.size() << "总:" << m_allThreads.size();
        }
    } else {
        qWarning() << "【线程池警告】回收线程失败：线程不在工作列表中，地址：" << thread;
    }
}

// 清空并回收线程池中的所有线程
void ThreadPool::clear()
{
    // 首先停止空闲检查定时器，避免后续跨线程操作 timer
    if (m_idleCheckTimer) {
        m_idleCheckTimer->stop();
    }

    QList<QThread*> toQuit;
    {
        QMutexLocker locker(&m_mutex);
        toQuit = m_workingThreads + m_idleThreads;
        m_workingThreads.clear();
        m_idleThreads.clear();
        m_allThreads.clear();
        m_idleThreadTimestamps.clear();
    }

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 3500;
    for (QThread* thread : toQuit) {
        qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
        if (remaining <= 0)
            break;
        // 先断开 finished，避免 quit 后槽排队到主线程里对「已死线程」做 SQL 清理或与 delete 竞态。
        QObject::disconnect(thread, &QThread::finished, nullptr, nullptr);
        // 在线程仍运行时关库（BlockingQueued）；m_threadConnections 仅存连接名，避免主线程析构 QSqlDatabase 触发 libmysql 断点。
        ConnectionPool::getInstance().removeThreadConnection(thread);
        const bool stopped = stopThreadGracefully(thread, static_cast<int>(qMin(remaining, 800LL)));
        if (!stopped && thread->isRunning()) {
            qWarning() << "【线程池-警告】线程未在超时内退出（clear），将等待其自行退出，地址：" << thread;
            continue;
        }
        ConnectionPool::getInstance().removeThreadConnection(thread);
        // 保底 wait，确保 QThread 内部事件分发器完全退出后再 delete。
        thread->wait(3000);
        delete thread;
    }
}

// 设置线程池允许的最大线程数
void ThreadPool::setMaxThreads(int num)
{
    if (num < 1) {
        qWarning() << "【线程池警告】设置最大线程数失败：数值需≥1，传入值：" << num;
        return;
    }
    MAX_THREAD_COUNT = num;
    qInfo() << "【线程池-配置更新】最大线程数已设置为：" << num;
}

// 返回线程池总线程数
int ThreadPool::totalThreads()
{
    QMutexLocker locker(&m_mutex);
    return m_allThreads.size();
}

// 返回当前工作线程数
int ThreadPool::workingThreads()
{
    QMutexLocker locker(&m_mutex);
    return m_workingThreads.size();
}

// 返回当前空闲线程数
int ThreadPool::idleThreads()
{
    QMutexLocker locker(&m_mutex);
    return m_idleThreads.size();
}

void ThreadPool::registerThreadCleanupOnFinished(QThread *thread)
{
    if (!thread)
        return;
    connect(thread, &QThread::finished, this, [this, thread]() {
        QMutexLocker locker(&m_mutex);
        m_workingThreads.removeOne(thread);
        m_idleThreads.removeOne(thread);
        m_allThreads.removeOne(thread);
        m_idleThreadTimestamps.remove(thread);

        ConnectionPool::getInstance().removeThreadConnection(thread);

        qInfo() << "【线程池-线程正常退出】线程地址：" << thread
                << "，剩余总线程数：" << m_allThreads.size();

        // 线程已发出 finished 信号，其事件循环即将/已经停止。
        // 直接 deleteLater 会尝试向已停止的事件循环投递事件，导致 Win32 wakeUp 失败。
        // 改为：已 wait() 确认线程完全退出后，在主线程安全 delete。
        thread->wait(3000);
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [thread]() {
                delete thread;
            }, Qt::QueuedConnection);
        } else {
            delete thread;
        }
    });
}

// 创建并启动一个新线程
QThread* ThreadPool::createNewThread()
{
    QThread* thread = new QThread();

    thread->start();
    registerThreadCleanupOnFinished(thread);

    return thread;
}

// 定时检查并释放超时空闲线程
void ThreadPool::checkAndReleaseIdleThreads()
{
    QList<QThread*> timeoutThreads;
    {
        QMutexLocker locker(&m_mutex);
        if (m_idleThreads.isEmpty()) return;

        const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        for (QThread* thread : m_idleThreads) {
            const qint64 last = m_idleThreadTimestamps.value(thread, 0);
            if (last <= 0 || (currentTime - last) >= IDLE_THREAD_TIMEOUT) {
                timeoutThreads.append(thread);
            }
        }

        if (timeoutThreads.isEmpty()) return;

        // 只从空闲队列摘掉，避免重复扫描 / getThread 误取。
        // 切勿在此处 m_allThreads.removeOne：线程仍在跑，若提前减少「已创建数」，
        // getThread 会认为未达上限而 new QThread，瞬时 OS 线程数可超过 MaxThreads，
        // 与连接池清理、事件队列叠加后极易不稳定（甚至 ACCESS_VIOLATION）。
        // 先断开 finished 信号再摘除列表，防止 removeOne 与 disconnect 之间
        // 线程自然结束触发 finished → registerThreadCleanupOnFinished 误操作。
        for (QThread* thread : timeoutThreads) {
            QObject::disconnect(thread, &QThread::finished, nullptr, nullptr);
            m_idleThreads.removeOne(thread);
            m_idleThreadTimestamps.remove(thread);
        }
    }

    qInfo() << "【线程池-开始回收超时空闲线程】共检测到" << timeoutThreads.size() << "个超时空闲线程";
    int stoppedCount = 0;
    for (QThread* thread : timeoutThreads) {
        if (!thread) continue;
        // 不持锁等待，避免阻塞 getThread/releaseThread 等路径。
        QObject::disconnect(thread, &QThread::finished, nullptr, nullptr);
        ConnectionPool::getInstance().removeThreadConnection(thread);
        const bool stopped = stopThreadGracefully(thread, 3000);
        if (!stopped && thread->isRunning()) {
            qWarning() << "【线程池-警告】线程未在超时内退出（idle 回收），将等待其自行退出，地址：" << thread;
            registerThreadCleanupOnFinished(thread);
            QMutexLocker locker(&m_mutex);
            m_idleThreads.append(thread);
            m_idleThreadTimestamps[thread] = QDateTime::currentMSecsSinceEpoch();
            continue;
        }
        stoppedCount++;
        // 线程已停：再摘一次连接/锚点（通常为 no-op），避免仅 stop 成功但 map 仍残留的边缘情况
        ConnectionPool::getInstance().removeThreadConnection(thread);
        {
            QMutexLocker locker(&m_mutex);
            m_allThreads.removeOne(thread);
        }
        // 保底 wait，确保 QThread 内部事件分发器完全退出，避免析构时
        // 触发 "QObject::killTimer: Timers cannot be stopped from another thread"
        // 或 "QEventDispatcherWin32::wakeUp: Failed to post a message"。
        thread->wait(3000);
        delete thread;
    }

    qInfo() << "【线程池-回收超时空闲线程完成】已回收" << stoppedCount
            << "个线程，未及时退出" << (timeoutThreads.size() - stoppedCount) << "个";
}
