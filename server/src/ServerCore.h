#ifndef SERVERCORE_H
#define SERVERCORE_H

/**
 * @file ServerCore.h
 * 业务核心：监听、客户端会话集合、与线程池/连接池协作。
 */

#include <QObject>
#include <QHash>
#include <QList>
#include <QVector>
#include <QPointer>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <functional>
#include <QStringList>
#include <QMutex>
#include <atomic>

class ClientHandler;
class QTcpServer;
class ServerTimers;

// 纯网络逻辑核心，无 GUI 依赖。负责 TCP 监听、连接管理、定时器调度。
// headless 模式下单独使用；GUI 模式下由 Server 持有并委托。
class ServerCore : public QObject
{
    Q_OBJECT

public:
    // 构造纯网络核心，不执行初始化。
    explicit ServerCore(QObject *parent = nullptr);
    // 析构时关闭监听、清理连接和线程池。
    ~ServerCore();

    // 加载配置并初始化，返回是否成功。headless 下需在构造后立即调用。
    bool initialize();
    // 启动 TCP 监听。
    bool startListening();
    // 停止监听并关闭所有连接。
    void stopListening();

    // 返回服务器是否处于关闭流程中。
    bool isShuttingDown() const { return m_shutdownFlag.load(); }
    // 返回监听器是否处于有效监听状态。
    bool isValid() const;

    // 返回当前配置的监听端口。
    quint16 listenPort() const { return m_listenPort; }
    // 返回客户端空闲超时毫秒数。
    int clientIdleTimeoutMs() const { return m_clientIdleTimeoutMs; }

    // 添加在线客户端映射。
    void addClient(quint64 accountId, const QPointer<ClientHandler> &client);
    // 按账号移除在线客户端映射。
    void removeClient(quint64 accountId, const QPointer<ClientHandler> &targetClient);
    // 获取指定账号的在线客户端对象。
    QPointer<ClientHandler> getClient(quint64 accountId);

    // 仅对有 pending 数据的连接调用 pumpDocSend（供 ServerTimers 使用）。
    void pumpDocSendForPendingClients();
    // 注册：连接有待发送的 doc 数据时注册。
    void registerClientForDocSendPump(ClientHandler *h);
    // 注销：队列清空时注销。
    void unregisterClientForDocSendPump(ClientHandler *h);

    // 获取所有 ClientHandler 列表（供 idle 检查）。
    QList<ClientHandler*> getAllClientHandlers() const;
    // 持锁快照后回调，避免 getAllClientHandlers() 每次分配 QList；回调内勿再依赖集合稳定性。
    void forEachClientHandler(const std::function<void(ClientHandler *)> &fn) const;
    // 注册：新连接创建时注册。
    void registerClientHandler(ClientHandler *h);
    // 注销：断开时注销。
    void unregisterClientHandler(ClientHandler *h);
    // 连接销毁/断线时从文档泵、上传清理、在线表等全部摘除（可重复调用）。
    void abandonConnectionResources(ClientHandler *h);

    // 仅对有活跃上传的连接调用 checkUploadCleanup（供 ServerTimers 使用）。
    void checkUploadCleanupForActiveClients();
    // 注册：m_uploads 非空时注册。
    void registerClientForUploadCleanup(ClientHandler *h);
    // 注销：m_uploads 清空时注销。
    void unregisterClientForUploadCleanup(ClientHandler *h);

    // 返回当前在线账号列表（用于日志等）。
    QList<quint64> getOnlineAccounts() const;

private:
    // 建立并启动 TCP 监听。
    bool tcpListen();
    // 处理新的 TCP 连接事件。
    void onNewConnection();

private:
    QTcpServer *m_tcp = nullptr;
    ServerTimers *m_timers = nullptr;
    bool m_listenFlag = false;
    quint16 m_listenPort = 0;
    int m_clientIdleTimeoutMs = 0;
    std::atomic<bool> m_shutdownFlag{false};

    // 所有连接
    QSet<ClientHandler*> m_allClientHandlers;

    // 通过账号查找客户端使用
    QHash<quint64, QPointer<ClientHandler>> m_clientsMap;
    // 与 m_clientsMap 同步：主连接断开时 O(1) 摘除账号映射（一账号一条主连接）。
    QHash<ClientHandler *, quint64> m_handlerToAccountId;


    // 当前有文档下发队列待发送的连接集合。
    QSet<ClientHandler*> m_clientsWithPendingDocSend;

    // 当前有上传会话在进行的连接集合
    QSet<ClientHandler*> m_clientsWithActiveUploads;

    // 目前维护上述结构的操作都是在IO线程集中操作 暂时不需要锁 同时也不应该有线程池内的线程访问接口
    // mutable QMutex m_allHandlersLock;
    // mutable QReadWriteLock m_clientsLock;
    // QMutex m_docSendPendingLock;
    // QMutex m_uploadCleanupLock;
};

#endif // SERVERCORE_H
