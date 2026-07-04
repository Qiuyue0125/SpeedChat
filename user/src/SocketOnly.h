#ifndef SOCKETONLY_H
#define SOCKETONLY_H

/**
 * @file SocketOnly.h
 * 独立线程 TCP 单例：收发队列、应用层心跳与断线感知。
 */

#include <QObject>
#include <QTcpSocket>
#include <QScopedPointer>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <functional>
#include <atomic>
#include <QTimer>
#include <QMetaObject>
#include <cstdint>

// 前置声明删除器模板
template <typename T>
struct QScopedPointerDeleter;

// 单例模式套接字通信类
// 独立线程管理套接字连接；断线感知依赖 QTcpSocket 的 disconnected/error、写失败 abort，
// 以及周期性 heart 带来的发送路径检测（不做应用层「长期无下行」轮询）
class SocketOnly : public QObject
{
    Q_OBJECT
    // 友元允许删除器访问私有析构
    template <typename T>
    friend struct QScopedPointerDeleter;

private:
    // 构造函数
    explicit SocketOnly(QObject* parent = nullptr);

    // 析构函数
    ~SocketOnly() override;

    // 提交套接字操作到工作线程
    template<typename Func>
    void postSocketOp(Func&& func) {
        if (!m_socketThread || m_isReleased) return;

        QMutexLocker locker(&m_opMutex);
        m_pendingOps.enqueue(std::forward<Func>(func));
        const bool needSchedule = !m_executeScheduled.exchange(true, std::memory_order_acq_rel);
        locker.unlock();
        if (needSchedule) {
            QMetaObject::invokeMethod(this, "executeSocketOp", Qt::QueuedConnection);
        }
    }

    // 执行已提交的套接字操作
    Q_INVOKABLE void executeSocketOp();

    // 清理套接字资源
    Q_INVOKABLE void cleanupSocketResources();

public:
    // 禁用拷贝构造和赋值运算符
    SocketOnly(const SocketOnly&) = delete;
    SocketOnly& operator=(const SocketOnly&) = delete;

    // 获取单例实例
    static SocketOnly& instance();

    // 获取套接字指针 仅用于连接信号槽
    QTcpSocket* socket();

    // 判断是否已连接
    bool isConnected() const;

    // 线程安全发送数据
    void sendData(const QByteArray& data);

    // 初始化或重建连接
    void initializeConnection();

    // 释放单例资源
    static void releaseInstance();

signals:
    // 连接成功信号
    void socketConnected();

    // 断开连接信号
    void socketDisconnected();

    // 错误信号
    void socketError(const QString& errorString);
    // 达到最大重连次数后失败（参数为已重试次数）
    void reconnectFailed(int retryCount);

    // 接收数据信号
    void dataReceived(const QByteArray& data);

private:
    // 单例对象
    static QScopedPointer<SocketOnly> m_instance;
    // 单例初始化互斥锁（保证 instance() 线程安全）
    static QMutex s_instanceMutex;
    // 工作线程
    QThread* m_socketThread = nullptr;
    // 套接字对象
    QTcpSocket* m_socket = nullptr;
    // 服务器地址
    QString m_hostName;
    // 服务器端口
    quint16 m_port = 0;
    // 应用层心跳间隔（毫秒，见 ClientConfigDefaults 代码常量）
    int m_heartbeatIntervalMs = 0;
    // 是否已释放
    bool m_isReleased = false;
    // 连接状态缓存：避免跨线程 BlockingQueuedConnection 卡死
    std::atomic_bool m_connected{false};
    // 心跳定时器
    QTimer* m_timer;
    // 断线重连定时器（运行在 socket 线程）
    QTimer* m_reconnectTimer = nullptr;
    // 重连退避计数
    int m_reconnectAttempts = 0;
    // 最大重连次数（超过后停止自动重连）
    int m_maxReconnectAttempts = 0;
    // 是否已发出重连失败信号（避免重复弹窗）
    bool m_reconnectFailedEmitted = false;
    // 操作锁
    QMutex m_opMutex;
    // 待执行操作队列（避免快速连续发送时被覆盖丢消息）
    QQueue<std::function<void()>> m_pendingOps;
    // 是否已调度 executeSocketOp（避免重复排队导致事件循环压力）
    std::atomic_bool m_executeScheduled{false};

    // 在 socket 线程调度一次带退避的重连
    void scheduleReconnect();

    // 手动重连时注册的 disconnected 槽连接（用于在再次调用前先断开，避免重复触发）
    QMetaObject::Connection m_manualReconnectConn;
};

#endif // 结束
