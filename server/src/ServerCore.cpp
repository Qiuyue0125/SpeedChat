/**
 * @file ServerCore.cpp
 * QTcpServer 监听、接受连接、线程池与定时器模块装配。
 */
#include "ServerCore.h"
#include "ClientHandler.h"
#include "ConnectionPool.h"
#include "ServerTimers.h"
#include "ThreadPool.h"
#include "ServerConfigDefaults.h"

#include <QPointer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QVector>

namespace {

// 确保指定目录存在，不存在则尝试创建。
void ensureDirExists(const QString &path)
{
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "目录创建失败:" << path;
    }
}

// 初始化服务端存储目录结构。
void ensureStorageLayout()
{
    const QString basePath = ServerConfigDefaults::storageBasePath();
    ensureDirExists(basePath);
    ensureDirExists(basePath + "audio/");
    ensureDirExists(basePath + "document/");
    ensureDirExists(basePath + "picture/");
    ensureDirExists(basePath + "ava/");
    ensureDirExists(basePath + "cloud/");  // 速聊网盘文件体
}

} // namespace

// 构造纯网络核心，不执行初始化。
ServerCore::ServerCore(QObject *parent)
    : QObject(parent)
{
}

// 析构时关闭监听、清理连接和线程池。
ServerCore::~ServerCore()
{
    m_shutdownFlag.store(true, std::memory_order_release);

    {
        // QWriteLocker locker(&m_clientsLock);
        m_clientsMap.clear();
        m_handlerToAccountId.clear();
    }

    if (m_tcp) {
        m_tcp->close();
    }
    if (m_timers) {
        m_timers->stop();
    }
    {
        // QMutexLocker locker(&m_allHandlersLock);
        m_allClientHandlers.clear();
    }
    ConnectionPool::getInstance().clearConnection();
    ThreadPool::getInstance().clear();
}

// 加载配置并初始化，返回是否成功。headless 下需在构造后立即调用。
bool ServerCore::initialize()
{
    ServerConfigDefaults::ensureServerDefaults();

    m_listenPort = ServerConfigDefaults::serverListenPort();
    m_clientIdleTimeoutMs = ServerConfigDefaults::serverClientIdleTimeoutMs();

    const int maxDb = ServerConfigDefaults::databasePoolMaxConnections();
    const int maxThreads = ServerConfigDefaults::threadPoolMaxThreads();
    ConnectionPool::getInstance().setMaxConnections(maxDb);
    ThreadPool::getInstance().setMaxThreads(maxThreads);

    qInfo() << "【Server配置】ListenPort=" << m_listenPort
            << "MaxDbConnections=" << maxDb
            << "MaxThreads=" << maxThreads
            << "ClientIdleTimeoutMs=" << m_clientIdleTimeoutMs;

    // 检查连接池初始化状态（驱动可用 + 表创建完成）
    if (!ConnectionPool::getInstance().isInitialized()) {
        qCritical() << "【Server-初始化失败】数据库连接池初始化失败：请检查 MySQL 驱动部署和数据库连接配置。";
        return false;
    }

    ensureStorageLayout();

    m_tcp = new QTcpServer(this);
    m_timers = new ServerTimers(this, this);
    return true;
}

// 启动 TCP 监听。
bool ServerCore::startListening()
{
    if (!tcpListen())
        return false;
    m_listenFlag = true;
    return true;
}

// 停止监听并关闭所有连接。
void ServerCore::stopListening()
{
    m_listenFlag = false;
    if (m_timers) m_timers->stop();
    if (m_tcp) m_tcp->close();
}

// 返回监听器是否处于有效监听状态。
bool ServerCore::isValid() const
{
    return m_tcp && m_tcp->isListening();
}

// 添加在线客户端映射。
void ServerCore::addClient(quint64 accountId, const QPointer<ClientHandler> &client)
{
    if (m_shutdownFlag.load(std::memory_order_acquire)) {
        qWarning() << "Server正在关闭，拒绝添加客户端:" << accountId;
        return;
    }
    // QWriteLocker locker(&m_clientsLock);
    const QPointer<ClientHandler> previous = m_clientsMap.value(accountId);
    if (previous && previous.data() != client.data()) {
        m_handlerToAccountId.remove(previous.data());
    }
    ClientHandler *const p = client.data();
    if (p) {
        const auto oldAccIt = m_handlerToAccountId.find(p);
        if (oldAccIt != m_handlerToAccountId.end() && oldAccIt.value() != accountId) {
            const quint64 oldAcc = oldAccIt.value();
            if (m_clientsMap.value(oldAcc).data() == p) {
                m_clientsMap.remove(oldAcc);
            }
        }
        m_handlerToAccountId.insert(p, accountId);
    }
    m_clientsMap.insert(accountId, client);
}

// 按账号移除在线客户端映射。
void ServerCore::removeClient(quint64 accountId, const QPointer<ClientHandler> &targetClient)
{
    if (m_shutdownFlag.load(std::memory_order_acquire)) {
        qWarning() << "Server正在关闭，拒绝移除客户端:" << accountId;
        return;
    }
    // QWriteLocker locker(&m_clientsLock);
    if (!m_clientsMap.contains(accountId)) {
        qWarning() << "客户端账号不存在，无法移除:" << accountId;
        return;
    }
    QPointer<ClientHandler> storedClient = m_clientsMap.value(accountId);
    if (storedClient != targetClient) return;
    m_clientsMap.remove(accountId);
    if (targetClient) {
        m_handlerToAccountId.remove(targetClient.data());
    }
}

// 获取指定账号的在线客户端对象。
QPointer<ClientHandler> ServerCore::getClient(quint64 accountId)
{
    if (m_shutdownFlag.load(std::memory_order_acquire))
        return QPointer<ClientHandler>();
    // QReadLocker locker(&m_clientsLock);
    return m_clientsMap.value(accountId);
}

// 仅对有 pending 数据的连接调用 pumpDocSend（供 ServerTimers 使用）。
void ServerCore::pumpDocSendForPendingClients()
{
    if (m_shutdownFlag.load(std::memory_order_acquire)) return;
    QVector<QPointer<ClientHandler>> toPump;
    {
        // QMutexLocker locker(&m_docSendPendingLock);
        toPump.reserve(m_clientsWithPendingDocSend.size());
        for (ClientHandler *h : m_clientsWithPendingDocSend) {
            toPump.append(QPointer<ClientHandler>(h));
        }
    }
    for (const QPointer<ClientHandler> &ptr : toPump) {
        if (ptr.isNull()) continue;
        ptr->pumpDocSend();
    }
}

// 注册：连接有待发送的 doc 数据时注册。
void ServerCore::registerClientForDocSendPump(ClientHandler *h)
{
    if (!h || m_shutdownFlag.load(std::memory_order_acquire)) return;
    if (!h->isTcpConnected()) return;
    bool needEnable = false;
    {
        // QMutexLocker locker(&m_docSendPendingLock);
        const bool wasEmpty = m_clientsWithPendingDocSend.isEmpty();
        m_clientsWithPendingDocSend.insert(h);
        needEnable = wasEmpty && !m_clientsWithPendingDocSend.isEmpty();
    }
    if (needEnable && m_timers) {
        m_timers->setDocSendEnabled(true);
    }
}

// 注销：队列清空时注销。
void ServerCore::unregisterClientForDocSendPump(ClientHandler *h)
{
    if (!h) return;
    bool needDisable = false;
    {
        // QMutexLocker locker(&m_docSendPendingLock);
        m_clientsWithPendingDocSend.remove(h);
        needDisable = m_clientsWithPendingDocSend.isEmpty();
    }
    if (needDisable && m_timers) {
        m_timers->setDocSendEnabled(false);
    }
}

// 获取所有 ClientHandler 列表（供 idle 检查）。
QList<ClientHandler*> ServerCore::getAllClientHandlers() const
{
    // QMutexLocker locker(&m_allHandlersLock);
    return m_allClientHandlers.values();
}

// 持锁快照后回调，避免 getAllClientHandlers() 每次分配 QList；回调内勿再依赖集合稳定性。
void ServerCore::forEachClientHandler(const std::function<void(ClientHandler *)> &fn) const
{
    if (!fn || m_shutdownFlag.load(std::memory_order_acquire)) return;
    QVector<QPointer<ClientHandler>> snapshot;
    {
        // QMutexLocker locker(&m_allHandlersLock);
        snapshot.reserve(m_allClientHandlers.size());
        for (ClientHandler *h : m_allClientHandlers) {
            snapshot.append(QPointer<ClientHandler>(h));
        }
    }
    for (const QPointer<ClientHandler> &ptr : snapshot) {
        if (!ptr.isNull()) fn(ptr.data());
    }
}

// 注册：新连接创建时注册。
void ServerCore::registerClientHandler(ClientHandler *h)
{
    if (!h || m_shutdownFlag.load(std::memory_order_acquire)) return;
    // QMutexLocker locker(&m_allHandlersLock);
    m_allClientHandlers.insert(h);
}

// 注销：断开时注销。
void ServerCore::unregisterClientHandler(ClientHandler *h)
{
    if (!h) return;
    // QMutexLocker locker(&m_allHandlersLock);
    m_allClientHandlers.remove(h);
}

// 连接销毁/断线时从文档泵、上传清理、在线表等全部摘除（可重复调用）。
void ServerCore::abandonConnectionResources(ClientHandler *h)
{
    if (!h) return;
    unregisterClientForDocSendPump(h);
    unregisterClientForUploadCleanup(h);
    unregisterClientHandler(h);
    {
        // QWriteLocker locker(&m_clientsLock);
        auto hit = m_handlerToAccountId.find(h);
        if (hit != m_handlerToAccountId.end()) {
            const quint64 acc = hit.value();
            m_handlerToAccountId.erase(hit);
            const QPointer<ClientHandler> stored = m_clientsMap.value(acc);
            if (stored.data() == h) {
                m_clientsMap.remove(acc);
            }
        }
    }
}

// 仅对有活跃上传的连接调用 checkUploadCleanup（供 ServerTimers 使用）。
void ServerCore::checkUploadCleanupForActiveClients()
{
    if (m_shutdownFlag.load(std::memory_order_acquire)) return;
    QVector<QPointer<ClientHandler>> toCheck;
    {
        // QMutexLocker locker(&m_uploadCleanupLock);
        toCheck.reserve(m_clientsWithActiveUploads.size());
        for (ClientHandler *h : m_clientsWithActiveUploads) {
            toCheck.append(QPointer<ClientHandler>(h));
        }
    }
    for (const QPointer<ClientHandler> &ptr : toCheck) {
        if (ptr.isNull()) continue;
        ptr->checkUploadCleanup();
    }
}

// 注册：m_uploads 非空时注册。
void ServerCore::registerClientForUploadCleanup(ClientHandler *h)
{
    if (!h || m_shutdownFlag.load(std::memory_order_acquire)) return;
    // QMutexLocker locker(&m_uploadCleanupLock);
    m_clientsWithActiveUploads.insert(h);
}

// 注销：m_uploads 清空时注销。
void ServerCore::unregisterClientForUploadCleanup(ClientHandler *h)
{
    if (!h) return;
    // QMutexLocker locker(&m_uploadCleanupLock);
    m_clientsWithActiveUploads.remove(h);
}

// 返回当前在线账号列表（用于日志等）。
QList<quint64> ServerCore::getOnlineAccounts() const
{
    // QReadLocker locker(&m_clientsLock);
    return m_clientsMap.keys();
}

// 建立并启动 TCP 监听。
bool ServerCore::tcpListen()
{
    if (!m_tcp || !m_timers) return false;
    disconnect(m_tcp, &QTcpServer::newConnection, this, &ServerCore::onNewConnection);
    connect(m_tcp, &QTcpServer::newConnection, this, &ServerCore::onNewConnection);

    if (!m_tcp->listen(QHostAddress::Any, m_listenPort)) {
        qCritical() << "监听失败:" << m_tcp->errorString();
        return false;
    }
    m_timers->start();
    qInfo() << "监听成功，端口" << m_listenPort;
    return true;
}

// 处理新的 TCP 连接事件。
void ServerCore::onNewConnection()
{
    if (!m_tcp) return;
    QTcpSocket *socket = m_tcp->nextPendingConnection();
    if (!socket) return;
    qInfo() << "新连接:" << socket->peerAddress().toString() << ":" << socket->peerPort();

    ClientHandler *worker = new ClientHandler(socket, this, m_clientIdleTimeoutMs, this);
    registerClientHandler(worker);
    worker->startHandling();
}
