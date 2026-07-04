/**
 * @file SocketOnly.cpp
 * 主连接套接字单例：独立线程、心跳、发送队列、断线退避重连。
 */
#include "SocketOnly.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QApplication>

#include <QSettings>
#include "ClientConfigDefaults.h"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QAbstractSocket>
#include "SocketDepend.h"
#include <QRandomGenerator>

// 单例对象
QScopedPointer<SocketOnly> SocketOnly::m_instance(nullptr);
QMutex SocketOnly::s_instanceMutex;

// 构造函数
SocketOnly::SocketOnly(QObject* parent)
    : QObject(parent)
    , m_isReleased(false)
{
    // 创建工作线程
    m_socketThread = new QThread(this);
    m_socketThread->setObjectName("SocketThread");

    // 读取配置
    ClientConfigDefaults::ensureClientDefaults();
    const QString configFilePath = ClientConfigDefaults::settingsIniPath();

    // 读取服务器地址和端口
    QSettings settings(configFilePath, QSettings::IniFormat);
    m_hostName = settings.value("Socket/HostName").toString();
    if (m_hostName.isEmpty()) m_hostName = ClientConfigDefaults::defaultSocketHostName();
    m_port = settings.value("Socket/Port").toUInt();
    if (m_port == 0) m_port = ClientConfigDefaults::defaultSocketPort();

    m_heartbeatIntervalMs = ClientConfigDefaults::defaultSocketHeartbeatIntervalMs();
    m_maxReconnectAttempts = ClientConfigDefaults::defaultSocketMaxReconnectAttempts();

    // 线程启动时初始化套接字
    connect(m_socketThread, &QThread::started, this, [=]() {
        // 创建套接字对象
        m_socket = new QTcpSocket();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

        // 绑定信号
        connect(m_socket, &QTcpSocket::connected, this, [this]() {
            m_connected.store(true, std::memory_order_release);
            m_reconnectAttempts = 0; // 成功连接后清零退避
            m_reconnectFailedEmitted = false;
            emit socketConnected();
        });
        connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            m_connected.store(false, std::memory_order_release);
            emit socketDisconnected();
            if (!m_isReleased) {
                scheduleReconnect();
            }
        });
        connect(m_socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError err) {
            Q_UNUSED(err);
            emit socketError(m_socket->errorString());
            m_connected.store(false, std::memory_order_release);
            if (!m_isReleased) {
                scheduleReconnect();
            }
        });
        connect(m_socket, &QTcpSocket::readyRead, this, [=]() {
            emit dataReceived(m_socket->readAll());
        });

        // 重连定时器（在 socket 线程内）
        if (!m_reconnectTimer) {
            m_reconnectTimer = new QTimer(this);
            m_reconnectTimer->setSingleShot(true);
            connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
                if (m_isReleased || !m_socket) return;
                if (m_socket->state() == QAbstractSocket::ConnectedState ||
                    m_socket->state() == QAbstractSocket::ConnectingState) {
                    return;
                }
                m_socket->connectToHost(m_hostName, m_port);
            });
        }

        // 连接服务器
        m_socket->connectToHost(m_hostName, m_port);
        qDebug() << "套接字初始化 线程" << QThread::currentThreadId();
    }, Qt::DirectConnection);

    // 移动对象到工作线程
    this->moveToThread(m_socketThread);

    // 启动工作线程
    m_socketThread->start();

    // 创建心跳定时器
    m_timer = new QTimer();
    m_timer->moveToThread(qApp->thread());
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [=]() {
        // 构造心跳数据
        QJsonObject jsonObj;
        jsonObj["tag"] = "heart";
        QJsonDocument jsonDoc(jsonObj);
        QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);

        // 发送心跳
        sendData(jsonData);
    }, Qt::QueuedConnection);

    // 设置心跳间隔
    m_timer->setInterval(m_heartbeatIntervalMs);
    m_timer->setSingleShot(false);
    m_timer->start();
}

// 析构函数
SocketOnly::~SocketOnly()
{
    qDebug() << "套接字析构 线程" << QThread::currentThreadId();
}

// 执行已提交的套接字操作
void SocketOnly::executeSocketOp() {
    while (true) {
        std::function<void()> op;
        {
            QMutexLocker locker(&m_opMutex);
            if (m_pendingOps.isEmpty()) {
                m_executeScheduled.store(false, std::memory_order_release);
                return;
            }
            op = m_pendingOps.dequeue();
        }
        if (op) op();
    }
}

// 清理套接字资源（必须在 socket 线程内调用，以便正确停止 m_reconnectTimer）
void SocketOnly::cleanupSocketResources()
{
    qDebug() << "正在清理套接字资源 线程" << QThread::currentThreadId();

    // 在 socket 线程内停止并释放重连定时器，避免析构时跨线程 killTimer
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }

    // 关闭并释放套接字资源
    if (m_socket) {
        m_connected.store(false, std::memory_order_release);
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        delete m_socket;
        m_socket = nullptr;
    }

    // 退出线程事件循环
    if (m_socketThread) {
        m_socketThread->requestInterruption();
        m_socketThread->quit();
    }

    // 标记实例已释放
    m_isReleased = true;
}

// 获取单例实例（线程安全）
SocketOnly& SocketOnly::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (m_instance.isNull() || m_instance->m_isReleased) {
        m_instance.reset(new SocketOnly());
    }
    return *m_instance;
}

// 获取套接字指针 仅用于连接信号槽
QTcpSocket* SocketOnly::socket()
{
    if (m_isReleased || !m_socket || (m_socketThread && m_socket->thread() != m_socketThread)) {
        return nullptr;
    }
    return m_socket;
}

// 判断是否已连接
bool SocketOnly::isConnected() const
{
    if (m_isReleased) return false;
    return m_connected.load(std::memory_order_acquire);
}

// 线程安全发送数据
void SocketOnly::sendData(const QByteArray& data)
{
    if (m_isReleased) {
        qWarning() << "套接字实例已释放";
        return;
    }

    // 在调用线程内完成 header+body 合并，避免在 socket 线程内做内存拷贝与多次 write
    ProtocolHeader header;
    header.dataLen = static_cast<uint32_t>(data.length());
    QByteArray headerBytes = headerToBytes(header);
    QByteArray combined;
    combined.reserve(headerBytes.size() + data.size());
    combined.append(headerBytes);
    combined.append(data);

    postSocketOp([this, combined]() {
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
            qWarning() << "套接字未连接 线程" << QThread::currentThreadId();
            if (m_connected.load(std::memory_order_acquire)) {
                m_connected.store(false, std::memory_order_release);
                emit socketDisconnected();
            }
            return;
        }
        const qint64 written = m_socket->write(combined);
        if (written != combined.size()) {
            qWarning() << "数据发送失败 长度" << written << "期望" << combined.size() << "线程" << QThread::currentThreadId();
            m_connected.store(false, std::memory_order_release);
            m_socket->abort();
        }
    });
}

// 初始化或重建连接
void SocketOnly::initializeConnection()
{
    if (m_isReleased) {
        qWarning() << "Socket instance has been released, reconnect failed";
        return;
    }

    // 线程安全执行重连逻辑
    postSocketOp([this]() {
        if (!m_socket) return;

        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            // 已连接则先断开，断开后自动重连；先断开旧的槽，避免多次点击重复注册
            if (m_manualReconnectConn) {
                QObject::disconnect(m_manualReconnectConn);
                m_manualReconnectConn = QMetaObject::Connection();
            }
            m_socket->disconnectFromHost();
            m_manualReconnectConn = connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
                QObject::disconnect(m_manualReconnectConn);
                m_manualReconnectConn = QMetaObject::Connection();
                m_reconnectAttempts = 0;
                m_reconnectFailedEmitted = false;
                if (m_socket) m_socket->connectToHost(m_hostName, m_port);
            });
        } else {
            // 未连接则直接重连
            m_reconnectAttempts = 0;
            m_reconnectFailedEmitted = false;
            m_socket->connectToHost(m_hostName, m_port);
        }
    });
}

// 释放单例资源（线程安全）
void SocketOnly::releaseInstance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (m_instance.isNull() || m_instance->m_isReleased) {
        qDebug() << "Socket instance already released";
        return;
    }

    SocketOnly* inst = m_instance.data();

    // 0. 在主线程清理 m_timer（m_timer 在 main thread，必须在同线程 stop/delete）
    if (inst->m_timer) {
        inst->m_timer->stop();
        delete inst->m_timer;
        inst->m_timer = nullptr;
    }

    if (inst->m_socketThread) {
        //1. 阻塞式执行资源清理（确保Socket线程内完成）
        QMetaObject::invokeMethod(inst, "cleanupSocketResources", Qt::BlockingQueuedConnection);

        //2. 等待线程退出（给足时间，避免 terminate 强杀导致不一致）
        if (inst->m_socketThread->isRunning()) {
            inst->m_socketThread->requestInterruption();
            inst->m_socketThread->quit();
            bool threadQuit = inst->m_socketThread->wait(10000);
            if (!threadQuit) {
                qCritical() << "Socket thread exit timeout, forcing terminate.";
                inst->m_socketThread->terminate();
                inst->m_socketThread->wait(1000);
            }
        }

        //3. 释放线程内存：仅在线程已退出时删除，避免 delete running thread
        if (!inst->m_socketThread->isRunning()) {
            delete inst->m_socketThread;
            inst->m_socketThread = nullptr;
        } else {
            qCritical() << "Socket thread still running after terminate; skip deleting thread.";
        }
    }

    //4. 重置单例指针，释放实例资源
    m_instance.reset();
    qDebug() << "Socket 释放";
}

// 在 socket 线程调度一次带退避的重连
void SocketOnly::scheduleReconnect()
{
    if (m_isReleased || !m_socket || !m_reconnectTimer) return;
    // 仅在 Unconnected 状态做退避重连，避免与正在连接的 connectToHost 竞争
    if (m_socket->state() != QAbstractSocket::UnconnectedState) return;

    if (m_reconnectAttempts >= m_maxReconnectAttempts) {
        if (!m_reconnectFailedEmitted) {
            m_reconnectFailedEmitted = true;
            emit reconnectFailed(m_reconnectAttempts);
        }
        return;
    }

    // 指数退避 + 抖动：250ms * 2^n，上限 30s，再加 0~250ms jitter
    m_reconnectAttempts = qMin(m_reconnectAttempts + 1, 16);
    const int base = 250;
    const int maxDelay = 30 * 1000;
    int delay = base * (1 << qMin(m_reconnectAttempts, 10)); // 最高约 256s，后面再 clamp
    delay = qMin(delay, maxDelay);
    delay += static_cast<int>(QRandomGenerator::global()->bounded(0, 250));
    m_reconnectTimer->start(delay);
}
