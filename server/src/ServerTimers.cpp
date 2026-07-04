/**
 * @file ServerTimers.cpp
 * 空闲检测、文档泵送、上传会话清理等周期任务。
 */
#include "ServerTimers.h"
#include "ServerConfigDefaults.h"
#include "ServerCore.h"
#include "ClientHandler.h"
#include <QDateTime>
#include <QPointer>

// 构造定时器，core 为 ServerCore 指针
ServerTimers::ServerTimers(ServerCore *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    m_idleCheckTimer = new QTimer(this);
    m_idleCheckTimer->setInterval(ServerConfigDefaults::defaultIdleCheckIntervalMs());
    m_idleCheckTimer->setSingleShot(false);
    connect(m_idleCheckTimer, &QTimer::timeout, this, &ServerTimers::onIdleCheckTick);

    m_docSendTimer = new QTimer(this);
    m_docSendTimer->setInterval(ServerConfigDefaults::defaultDocSendIntervalMs());
    m_docSendTimer->setSingleShot(false);
    connect(m_docSendTimer, &QTimer::timeout, this, &ServerTimers::onDocSendTick);

    m_uploadCleanupTimer = new QTimer(this);
    m_uploadCleanupTimer->setInterval(ServerConfigDefaults::defaultUploadCleanupIntervalMs());
    m_uploadCleanupTimer->setSingleShot(false);
    connect(m_uploadCleanupTimer, &QTimer::timeout, this, &ServerTimers::onUploadCleanupTick);
}

// 析构时停止所有定时器。
ServerTimers::~ServerTimers()
{
    stop();
}

// 启动所有定时器（空闲检查、文档发送泵送、上传清理）。
void ServerTimers::start()
{
    if (m_idleCheckTimer && !m_idleCheckTimer->isActive())
        m_idleCheckTimer->start();
    // docSendTimer 由 ServerCore 按需启停，避免空闲时 10ms tick 空转。
    if (m_uploadCleanupTimer && !m_uploadCleanupTimer->isActive())
        m_uploadCleanupTimer->start();
}

// 停止所有定时器。
void ServerTimers::stop()
{
    if (m_idleCheckTimer) m_idleCheckTimer->stop();
    if (m_docSendTimer) m_docSendTimer->stop();
    if (m_uploadCleanupTimer) m_uploadCleanupTimer->stop();
}

// 按需启停文档发送泵送定时器（无 pending 时可关闭 10ms tick）。
void ServerTimers::setDocSendEnabled(bool enabled)
{
    if (!m_docSendTimer) return;
    if (enabled) {
        if (!m_docSendTimer->isActive()) {
            m_docSendTimer->start();
        }
        return;
    }
    if (m_docSendTimer->isActive()) {
        m_docSendTimer->stop();
    }
}

// 遍历所有连接，调用 checkIdleTimeout（使用 Server 维护的列表）。
void ServerTimers::onIdleCheckTick()
{
    if (!m_core || m_core->isShuttingDown()) return;
    m_core->forEachClientHandler([](ClientHandler *h) {
        QPointer<ClientHandler> ptr(h);
        if (!ptr.isNull()) h->checkIdleTimeout();
    });
}

// 仅对有 pending 数据的连接调用 pumpDocSend。
void ServerTimers::onDocSendTick()
{
    if (!m_core || m_core->isShuttingDown()) return;
    m_core->pumpDocSendForPendingClients();
}

// 仅对有活跃上传的连接调用 checkUploadCleanup。
void ServerTimers::onUploadCleanupTick()
{
    if (!m_core || m_core->isShuttingDown()) return;
    m_core->checkUploadCleanupForActiveClients();
}
