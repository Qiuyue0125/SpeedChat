#ifndef THREADPOOL_H
#define THREADPOOL_H

/**
 * @file ThreadPool.h
 * 轻量业务线程池：任务队列与空闲回收。
 */

#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QList>
#include <QDebug>
#include <QObject>
#include <QDateTime>
#include "ServerConfigDefaults.h"

// 轻量线程池：复用线程，空闲超时回收，线程退出时清理数据库连接。
class ThreadPool : public QObject
{
    Q_OBJECT
private:
    // 构造线程池并初始化默认参数。
    explicit ThreadPool(QObject *parent = nullptr);
    // 析构线程池并回收全部线程。
    ~ThreadPool();

    // 禁止拷贝，确保单例语义。
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    QMutex m_mutex;
    QList<QThread*> m_idleThreads;
    QList<QThread*> m_workingThreads;
    int MAX_THREAD_COUNT = 0;
    QList<QThread*> m_allThreads;
    QMap<QThread*, qint64> m_idleThreadTimestamps;
    int IDLE_THREAD_TIMEOUT = 0;
    int DB_CHECK_INTERVAL = 0;
    QTimer *m_idleCheckTimer = nullptr;

public:
    // 获取线程池单例实例。
    static ThreadPool& getInstance();
    // 获取一个可用工作线程（复用或新建）。
    QThread* getThread();
    // 释放线程回线程池空闲列表。
    void releaseThread(QThread* thread);
    // 清空并回收线程池中的所有线程。
    void clear();
    // 设置线程池允许的最大线程数。
    void setMaxThreads(int num);

    // 返回线程池总线程数。
    int totalThreads();
    // 返回当前工作线程数。
    int workingThreads();
    // 返回当前空闲线程数。
    int idleThreads();

private slots:
    // 定时检查并释放超时空闲线程。
    void checkAndReleaseIdleThreads();

private:
    // 创建并启动一个新线程。
    QThread* createNewThread();
    // 线程自然退出时：摘表、清 DB 连接、deleteLater（与 createNewThread 共用）。
    void registerThreadCleanupOnFinished(QThread *thread);
};

#endif
