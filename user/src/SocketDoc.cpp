/**
 * @file SocketDoc.cpp
 * 文档传输专用连接：上传/下载会话、二进制分片与进度信号。
 */
#include "SocketDoc.h"
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <QFile>
#include <QSettings>
#include "ClientConfigDefaults.h"

namespace {
const int UPLOAD_CHUNK_INTERVAL_MS = 2;
}
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>
#include <QScopedPointer>
#include <QDir>

// 文件发送类实现
QScopedPointer<SocketDocWrite> SocketDocWrite::m_instance(nullptr);
QMutex SocketDocWrite::s_instanceMutex;

// 构造函数
SocketDocWrite::SocketDocWrite(QObject* parent)
    : QObject(parent)
    , m_recvState(RecvState::WaitHeader)
    , m_expectedDataLen(0)
    , m_socketThread(nullptr)
    , m_socket(nullptr)
    , m_isReleased(false)
    , m_timer(nullptr)
    , m_isFileSending(false)
    , m_isPendingData(false)
    , m_instanceMutex()
{
    ClientConfigDefaults::ensureClientDefaults();
    const QString configFilePath = ClientConfigDefaults::settingsIniPath();

    // 读取服务器配置
    QSettings settings(configFilePath, QSettings::IniFormat);
    m_hostName = settings.value("Socket/HostName").toString();
    if (m_hostName.isEmpty()) m_hostName = ClientConfigDefaults::defaultSocketHostName();
    m_port = settings.value("Socket/Port").toUInt();
    if (m_port == 0) m_port = ClientConfigDefaults::defaultSocketPort();

    m_uploadRateLimitEnabled = settings.value("Upload/RateLimitEnabled",
                                              ClientConfigDefaults::defaultUploadRateLimitEnabled())
                                   .toBool();
    m_uploadRateBytesPerSec = settings.value("Upload/UploadRateBytesPerSec",
                                               ClientConfigDefaults::defaultUploadRateBytesPerSec())
                                  .toInt();
    if (m_uploadRateBytesPerSec <= 0) {
        m_uploadRateBytesPerSec = ClientConfigDefaults::defaultUploadRateBytesPerSec();
    }
    m_uploadBurstBytes = settings.value("Upload/UploadBurstBytes",
                                         ClientConfigDefaults::defaultUploadBurstBytes())
                             .toInt();
    if (m_uploadBurstBytes <= 0) {
        m_uploadBurstBytes = ClientConfigDefaults::defaultUploadBurstBytes();
    }

    m_uploadTimer = new QTimer(this);
    m_uploadTimer->setInterval(UPLOAD_CHUNK_INTERVAL_MS);
    m_uploadTimer->setSingleShot(false);
    connect(m_uploadTimer, &QTimer::timeout, this, [this]() {
        if (m_isReleased) return;
        sendNextChunk();
    }, Qt::DirectConnection);
}

// 析构函数
SocketDocWrite::~SocketDocWrite()
{
    qDebug() << "SocketDocWrite 析构";
}

// 获取单例实例（线程安全）
SocketDocWrite& SocketDocWrite::instance()
{
    QMutexLocker lock(&s_instanceMutex);
    if (m_instance.isNull() || m_instance->m_isReleased) {
        m_instance.reset(new SocketDocWrite());
    }
    return *m_instance;
}

// 线程安全发送数据
void SocketDocWrite::sendData(const QByteArray& data, const QString& filename)
{
    if (m_isReleased) {
        qWarning() << "SocketDocWrite instance has been released, send failed";
        return;
    }

    m_isFileSending = true;

    {
        QMutexLocker lock(&m_instanceMutex);
        if (m_isReleased) return;
        if (!m_socketThread) {
            qDebug() << "文件发送请求，创建SocketDocWrite线程";
            m_socketThread = new QThread(nullptr);
            m_socketThread->setObjectName("SocketThread");
            connect(m_socketThread, &QThread::finished, m_socketThread, &QThread::deleteLater);

            this->moveToThread(m_socketThread);
            m_socketThread->start();
        }
    }

    if (m_socketThread == QThread::currentThread()) {
        executeSocketOp(data, filename, false);
    } else {
        QMetaObject::invokeMethod(this, "executeSocketOp",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data),
                                  Q_ARG(QString, filename),
                                  Q_ARG(bool, false));
    }
}

// 发送文件
void SocketDocWrite::sendFile(const QString &filePath,
                              const QString &sender,
                              const QString &receiver,
                              const QString &filename,
                              const QString &timestamp,
                              const QString &uuid)
{
    if (m_isReleased) return;

    {
        QMutexLocker lock(&m_instanceMutex);
        if (m_isReleased) return;
        if (!m_socketThread) {
            m_socketThread = new QThread(nullptr);
            m_socketThread->setObjectName("SocketThread");
            connect(m_socketThread, &QThread::finished, m_socketThread, &QThread::deleteLater);
            this->moveToThread(m_socketThread);
            m_socketThread->start();
        }
    }

    if (m_socketThread == QThread::currentThread()) {
        startSendFile(filePath, sender, receiver, filename, timestamp, uuid);
    } else {
        QMetaObject::invokeMethod(this, "startSendFile",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, filePath),
                                  Q_ARG(QString, sender),
                                  Q_ARG(QString, receiver),
                                  Q_ARG(QString, filename),
                                  Q_ARG(QString, timestamp),
                                  Q_ARG(QString, uuid),
                                  Q_ARG(bool, false));
    }
}

// 上传文件到网盘
void SocketDocWrite::sendCloudFile(const QString &filePath,
                                   const QString &sender,
                                   const QString &filename,
                                   const QString &timestamp,
                                   const QString &uuid)
{
    if (m_isReleased) return;

    {
        QMutexLocker lock(&m_instanceMutex);
        if (m_isReleased) return;
        if (!m_socketThread) {
            m_socketThread = new QThread(nullptr);
            m_socketThread->setObjectName("SocketThread");
            connect(m_socketThread, &QThread::finished, m_socketThread, &QThread::deleteLater);
            this->moveToThread(m_socketThread);
            m_socketThread->start();
        }
    }

    if (m_socketThread == QThread::currentThread()) {
        startSendFile(filePath, sender, sender, filename, timestamp, uuid, true);
    } else {
        QMetaObject::invokeMethod(this, "startSendFile",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, filePath),
                                  Q_ARG(QString, sender),
                                  Q_ARG(QString, sender),
                                  Q_ARG(QString, filename),
                                  Q_ARG(QString, timestamp),
                                  Q_ARG(QString, uuid),
                                  Q_ARG(bool, true));
    }
}

// 手动释放单例资源 释放后需要重新获取实例（线程安全）
void SocketDocWrite::releaseInstance()
{
    QMutexLocker lock(&s_instanceMutex);
    if (m_instance.isNull()) {
        qDebug() << "SocketDocWrite already released, skip";
        return;
    }

    SocketDocWrite* inst = m_instance.data();
    qDebug() << "开始清理SocketDocWrite (线程状态:" << (inst->m_socketThread ? "存在" : "不存在") << ")";

    // 在 socket 线程内执行资源清理（主线程退出时通过 BlockingQueuedConnection 确保执行）
    if (inst->m_socketThread && inst->m_socketThread->isRunning()) {
        if (QThread::currentThread() == inst->m_socketThread) {
            inst->cleanupSocketResources();
        } else {
            QMetaObject::invokeMethod(inst, "cleanupSocketResources", Qt::BlockingQueuedConnection);
        }
    }

    //重置状态变量（加锁防止与 sendFile/sendData 创建线程竞争）
    {
        QMutexLocker instLock(&inst->m_instanceMutex);
        inst->m_isFileSending = false;
        inst->m_recvState = RecvState::WaitHeader;
        inst->m_expectedDataLen = 0;
        inst->m_isReleased = true;
    }

    //线程退出逻辑：异步quit，不等待
    if (inst->m_socketThread) {
        if (inst->m_socketThread->isRunning()) {
            QMetaObject::invokeMethod(inst->m_socketThread, "quit", Qt::QueuedConnection);
        }
        inst->m_socketThread->setParent(nullptr);
        inst->m_socketThread = nullptr; //置空，线程finished后自动deleteLater
    }

    //移回主线程并重置单例
    inst->moveToThread(QCoreApplication::instance()->thread());
    m_instance.reset();
    qDebug() << "SocketDocWrite清理完成";
}

// 重置上传会话状态，必要时断开socket强制下次重连
void SocketDocWrite::resetUploadSession(bool closeSocket)
{
    if (m_uploadTimer && m_uploadTimer->isActive()) {
        m_uploadTimer->stop();
    }
    if (m_uploadFile.isOpen()) {
        m_uploadFile.close();
    }
    m_uploadUuid.clear();
    m_uploadSender.clear();
    m_uploadReceiver.clear();
    m_uploadFilename.clear();
    m_uploadTimestamp.clear();
    m_uploadTotalBytes = 0;
    m_uploadSentBytes = 0;
    m_lastUploadProgressPercent = -1;
    m_uploadSeq = 0;
    m_uploadTokens = 0;
    m_uploadLastRefillMs = 0;
    m_uploadIsCloud = false;
    m_cloudUploadNotified = false;

    if (closeSocket && m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

// 启动发送文件任务；isCloud 为 true 时 upload_begin 携带 cloud=true（网盘，receiver 通常等于 sender）
void SocketDocWrite::startSendFile(const QString &filePath,
                                   const QString &sender,
                                   const QString &receiver,
                                   const QString &filename,
                                   const QString &timestamp,
                                   const QString &uuid,
                                   bool isCloud)
{
    if (m_isReleased) return;

    if (m_uploadFile.isOpen()) {
        m_uploadFile.close();
    }
    m_uploadFile.setFileName(filePath);
    if (!m_uploadFile.open(QIODevice::ReadOnly)) {
        emit socketError("无法打开文件: " + filePath);
        return;
    }

    m_uploadUuid = uuid;
    m_uploadSender = sender;
    m_uploadReceiver = receiver;
    m_uploadFilename = filename;
    m_uploadTimestamp = timestamp;
    m_uploadIsCloud = isCloud;
    m_uploadTotalBytes = m_uploadFile.size();
    m_uploadSentBytes = 0;
    m_lastUploadProgressPercent = -1;
    m_uploadSeq = 0;
    m_uploadTokens = m_uploadBurstBytes;
    m_uploadLastRefillMs = QDateTime::currentMSecsSinceEpoch();

    // 确保socket连接
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        if (m_socket) {
            m_socket->abort();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_socket = new QTcpSocket();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(m_socket, &QTcpSocket::connected, this, &SocketDocWrite::socketConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &SocketDocWrite::socketDisconnected);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError err) {
            Q_UNUSED(err);
            const QString errText = m_socket ? m_socket->errorString() : QString();
            resetUploadSession(true);
            emit socketError(errText);
            m_isPendingData = false;
            m_pendingFileData = PendingFileData();
            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
        });
        // readyRead handler已经在executeSocketOp里绑定过逻辑；这里简单复用：直接调用executeSocketOp发送begin
        connect(m_socket, &QTcpSocket::connected, this, [this]() {
            // begin
            QJsonObject begin;
            begin["tag"] = "upload_begin";
            begin["uuid"] = m_uploadUuid;
            begin["sender"] = m_uploadSender;
            begin["receiver"] = m_uploadReceiver;
            begin["filename"] = m_uploadFilename;
            begin["timestamp"] = m_uploadTimestamp;
            begin["total_bytes"] = QString::number(m_uploadTotalBytes);
            if (m_uploadIsCloud) {
                begin["cloud"] = true;
            }
            QJsonDocument doc(begin);
            executeSocketOp(doc.toJson(QJsonDocument::Compact), m_uploadFilename, false);
        }, Qt::DirectConnection);
        disconnect(m_socket, &QTcpSocket::readyRead, this, &SocketDocWrite::onSocketReadyRead);
        connect(m_socket, &QTcpSocket::readyRead, this, &SocketDocWrite::onSocketReadyRead, Qt::DirectConnection);

        m_socket->connectToHost(m_hostName, m_port);
        return;
    }

    // 已连接，直接发begin
    QJsonObject begin;
    begin["tag"] = "upload_begin";
    begin["uuid"] = m_uploadUuid;
    begin["sender"] = m_uploadSender;
    begin["receiver"] = m_uploadReceiver;
    begin["filename"] = m_uploadFilename;
    begin["timestamp"] = m_uploadTimestamp;
    begin["total_bytes"] = QString::number(m_uploadTotalBytes);
    if (m_uploadIsCloud) {
        begin["cloud"] = true;
    }
    QJsonDocument doc(begin);
    executeSocketOp(doc.toJson(QJsonDocument::Compact), m_uploadFilename, false);
}

// 发送下一片文件数据
void SocketDocWrite::sendNextChunk()
{
    if (m_isReleased) return;
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    if (!m_uploadFile.isOpen()) return;

    const int maxBurstsPerTick = 48;
    for (int burst = 0; burst < maxBurstsPerTick; ++burst) {
        if (m_uploadRateLimitEnabled) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (m_uploadLastRefillMs <= 0) m_uploadLastRefillMs = nowMs;
            const qint64 deltaMs = qMax<qint64>(0, nowMs - m_uploadLastRefillMs);
            if (deltaMs > 0) {
                const qint64 add = (static_cast<qint64>(m_uploadRateBytesPerSec) * deltaMs) / 1000;
                m_uploadTokens = qMin<qint64>(m_uploadBurstBytes, m_uploadTokens + add);
                m_uploadLastRefillMs = nowMs;
            }
        }

        if (m_uploadFile.atEnd()) {
            if (m_uploadTimer->isActive()) m_uploadTimer->stop();
            QJsonObject end;
            end["tag"] = "upload_end";
            end["uuid"] = m_uploadUuid;
            QJsonDocument doc(end);
            executeSocketOp(doc.toJson(QJsonDocument::Compact), m_uploadFilename, false);
            return;
        }

        int want = m_uploadChunkBytes;
        if (m_uploadRateLimitEnabled) {
            want = qMin<int>(m_uploadChunkBytes, static_cast<int>(m_uploadTokens));
            if (want <= 0) return;
        }

        QByteArray bin = m_uploadFile.read(want);
        if (bin.isEmpty()) return;
        if (m_uploadRateLimitEnabled) {
            m_uploadTokens -= bin.size();
        }

        // 二进制分片帧（避免JSON+base64膨胀）
        // body: [frameType=1][uuid(16)][seq(u32)][chunkLen(u32)][chunkBytes]
        QByteArray body;
        body.reserve(1 + 16 + 4 + 4 + bin.size());
        body.append(char(1));

        const QUuid qu = QUuid::fromString(m_uploadUuid);
        const QByteArray uuidBin = qu.isNull() ? QUuid::fromString("{" + m_uploadUuid + "}").toRfc4122() : qu.toRfc4122();
        if (uuidBin.size() != 16) {
            return;
        }
        body.append(uuidBin);

        const uint32_t seqNet = hostToNetwork32(static_cast<uint32_t>(m_uploadSeq++));
        body.append(reinterpret_cast<const char*>(&seqNet), 4);

        const uint32_t lenNet = hostToNetwork32(static_cast<uint32_t>(bin.size()));
        body.append(reinterpret_cast<const char*>(&lenNet), 4);

        body.append(bin);

        const QByteArray headerBytes = makeHeaderBytes(static_cast<uint32_t>(body.size()), PAYLOAD_BINARY);
        if (m_socket->write(headerBytes) != PROTOCOL_HEADER_LEN) {
            return;
        }
        m_socket->write(body);
        m_uploadSentBytes += bin.size();
    }
}

// 执行套接字操作
void SocketDocWrite::executeSocketOp(const QByteArray& data, const QString& filename, bool isPending)
{
    Q_UNUSED(isPending);
    if (m_isReleased) return;

    // 套接字未连接时重建连接
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        // 清理旧套接字
        if (m_socket) {
            m_socket->abort();
            m_socket->deleteLater();
            m_socket = nullptr;
        }

        // 创建新套接字并配置
        m_socket = new QTcpSocket();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        // 连接套接字信号槽
        connect(m_socket, &QTcpSocket::connected, this, &SocketDocWrite::socketConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &SocketDocWrite::socketDisconnected);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError err) {
            Q_UNUSED(err);
            const QString errText = m_socket ? m_socket->errorString() : QString();
            resetUploadSession(true);
            emit socketError(errText);
            m_isPendingData = false;
            m_pendingFileData = PendingFileData();
            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
        });

        // 连接成功后发送缓存数据
        connect(m_socket, &QTcpSocket::connected, this, &SocketDocWrite::sendPendingData, Qt::DirectConnection);

        // 处理接收数据
        disconnect(m_socket, &QTcpSocket::readyRead, this, &SocketDocWrite::onSocketReadyRead);
        connect(m_socket, &QTcpSocket::readyRead, this, &SocketDocWrite::onSocketReadyRead, Qt::DirectConnection);

        // 连接服务器
        m_socket->connectToHost(m_hostName, m_port);
        qDebug() << "SocketDocWrite 重建Socket并连接服务器";

        // 子线程内创建并启动心跳定时器
        if (!m_timer) {
            m_timer = new QTimer(this);
            m_timer->setInterval(35 * 1000);
            m_timer->setSingleShot(false);
            connect(m_timer, &QTimer::timeout, this, [=]() {
                bool connected = false;
                if (m_socket && !m_isReleased && QThread::currentThread() == m_socketThread) {
                    connected = (m_socket->state() == QAbstractSocket::ConnectedState);
                }
                if (connected) {
                    // 发送心跳包
                    QJsonObject jsonObj;
                    jsonObj["tag"] = "heart";
                    QJsonDocument jsonDoc(jsonObj);
                    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
                    sendData(jsonData, "");
                }
            }, Qt::DirectConnection);
        }
        m_timer->start();

        // 缓存数据等待连接成功后发送
        m_pendingFileData = {data, filename};
        m_isPendingData = true;
        return;
    }

    // 套接字已连接直接发送数据
    ProtocolHeader header;
    header.dataLen = static_cast<uint32_t>(data.length());
    QByteArray headerBytes = headerToBytes(header);

    //发送协议头
    const qint64 headerWriteLen = m_socket->write(headerBytes);
    if (headerWriteLen != PROTOCOL_HEADER_LEN) {
        qWarning() << "SocketDocWrite 8字节协议头发送失败，实际发送：" << headerWriteLen << "字节";
        return;
    }
    m_socket->write(data);

    //标记文件发送状态
    if(!filename.isEmpty()){
        m_isFileSending = true;
    }
}

// 清理套接字相关资源 关闭连接释放对象并重置状态
void SocketDocWrite::cleanupSocketResources()
{
    qDebug() << "执行SocketDocWrite资源清理";

    // 停止定时器
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    // 关闭套接字
    if (m_socket) {
        m_socket->disconnect();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_isPendingData = false;
    m_pendingFileData = PendingFileData();
    m_isFileSending = false;
    m_recvState = RecvState::WaitHeader;
    m_expectedDataLen = 0;
    m_isReleased = true;
}

// 处理服务器返回的文件完成标识
void SocketDocWrite::handleDocumentDone()
{
    qDebug() << "收到上传完成通知，断开Socket连接";
    resetUploadSession(false);
    m_isFileSending = false;

    // 断开套接字
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    // 停止定时器
    if (m_timer) {
        m_timer->stop();
        m_timer = nullptr;
    }

    if (m_socketThread && m_socketThread->isRunning()) {
        QMetaObject::invokeMethod(this, "releaseInstance", Qt::QueuedConnection);
    } else {
        releaseInstance();
    }
}

// 发送缓存的文件数据
void SocketDocWrite::sendPendingData()
{
    if (m_isReleased || !m_isPendingData || m_pendingFileData.data.isEmpty() || !m_socket) {
        return;
    }

    qDebug() << "SocketDocWrite连接成功，发送缓存文件数据";

    // 构造协议头
    ProtocolHeader header;
    header.dataLen = static_cast<uint32_t>(m_pendingFileData.data.length());
    QByteArray headerBytes = headerToBytes(header);

    // 发送：QTcpSocket 自带缓冲与异步写，避免循环分块 + 每块 flush 造成抖动
    const qint64 headerWriteLen = m_socket->write(headerBytes);
    if (headerWriteLen != PROTOCOL_HEADER_LEN) {
        qWarning() << "SocketDocWrite 8字节协议头发送失败，实际发送：" << headerWriteLen << "字节";
        m_isPendingData = false;
        m_pendingFileData = PendingFileData();
        return;
    }
    m_socket->write(m_pendingFileData.data);

    // 清空缓存
    m_isPendingData = false;
    qDebug() << "SocketDocWrite缓存文件数据发送完成";
}

// 处理套接字可读事件
void SocketDocWrite::onSocketReadyRead()
{
    if (m_isReleased || !m_socket) return;
    const QByteArray recvData = m_socket->readAll();
    if (recvData.isEmpty()) return;
    m_incomingBuffer += recvData;

    while (!m_incomingBuffer.isEmpty() && !m_isReleased) {
        switch (m_recvState) {
        case RecvState::WaitHeader: {
            if (m_incomingBuffer.length() < PROTOCOL_HEADER_LEN) {
                return;
            }
            ProtocolHeader header = bytesToHeader(m_incomingBuffer.left(PROTOCOL_HEADER_LEN));
            if (header.version != PROTOCOL_WIRE_VERSION) {
                m_incomingBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                m_expectedDataLen = 0;
                return;
            }
            m_expectedDataLen = header.dataLen;
            if (m_expectedDataLen == 0 || m_expectedDataLen > MAX_PACKET_SIZE) {
                m_incomingBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                m_expectedDataLen = 0;
                return;
            }
            m_incomingBuffer = m_incomingBuffer.mid(PROTOCOL_HEADER_LEN);
            m_recvState = RecvState::WaitData;
            break;
        }
        case RecvState::WaitData: {
            if (m_incomingBuffer.length() < static_cast<int>(m_expectedDataLen)) {
                return;
            }
            const QByteArray completeData = m_incomingBuffer.left(m_expectedDataLen);
            m_incomingBuffer = m_incomingBuffer.mid(m_expectedDataLen);
            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
            handleIncomingJsonPacket(completeData);
            break;
        }
        }
    }
}

// 处理收到的完整协议数据
void SocketDocWrite::handleIncomingJsonPacket(const QByteArray &completeData)
{
    QJsonParseError parseErr{};
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(completeData, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !jsonDoc.isObject()) return;
    const QJsonObject jsonObj = jsonDoc.object();
    const QString tag = jsonObj.value("tag").toString().trimmed();

    // 网盘上传完成：优先 cloud_upload_done，兼容 messagehavedone 旧路径
    const auto notifyCloudUploadDone = [this, &jsonObj]() {
        if (!m_uploadIsCloud || m_cloudUploadNotified) {
            return;
        }
        m_cloudUploadNotified = true;
        const QString fileId = jsonObj.value("file_id").toString(jsonObj.value("uuid").toString(m_uploadUuid));
        const QString name = jsonObj.value("filename").toString(m_uploadFilename);
        const qint64 size = jsonObj.value("file_size").toString().toLongLong();
        const qint64 resolvedSize = size > 0 ? size : m_uploadTotalBytes;
        emit cloudFileUploaded(fileId, name, resolvedSize);
    };

    if (tag == "messagehavedone") {
        if (m_uploadIsCloud) {
            notifyCloudUploadDone();
            return;
        }
        const QString u = jsonObj.value("uuid").toString();
        const QString name = !m_uploadFilename.isEmpty() ? m_uploadFilename : m_pendingFileData.filename;
        emit fileUpdate(name, u);
        m_pendingFileData = PendingFileData();
        return;
    }
    if (tag == "cloud_upload_done") {
        notifyCloudUploadDone();
        return;
    }
    if (tag == "upload_ack") {
        if (m_cloudUploadNotified) {
            return;
        }
        const QString uuid = jsonObj.value("uuid").toString();
        const QJsonValue rb = jsonObj.value("received_bytes");
        const QJsonValue tb = jsonObj.value("total_bytes");
        const qint64 receivedBytes = rb.isString() ? rb.toString().toLongLong()
                                                   : rb.toVariant().toLongLong();
        const qint64 totalBytes = tb.isString() ? tb.toString().toLongLong()
                                                : tb.toVariant().toLongLong();
        if (!uuid.isEmpty() && totalBytes > 0) {
            int percent = static_cast<int>((receivedBytes * 100) / totalBytes);
            percent = qBound(0, percent, 99);
            if (percent > m_lastUploadProgressPercent
                || (percent - m_lastUploadProgressPercent >= 1)
                || m_lastUploadProgressPercent < 0) {
                m_lastUploadProgressPercent = percent;
                emit uploadProgress(uuid, receivedBytes, totalBytes);
            }
        }
        return;
    }
    if (tag == "upload_begin_ack") {
        const int cb = jsonObj.value("chunk_bytes").toVariant().toInt();
        if (cb > 0) m_uploadChunkBytes = cb;
        if (!m_uploadTimer->isActive()) m_uploadTimer->start();
        return;
    }
    if (tag == "upload_error") {
        resetUploadSession(true);
        emit socketError("上传失败: " + jsonObj.value("message").toString());
        return;
    }
    if (tag == "uploaddone") {
        if (m_uploadIsCloud) {
            notifyCloudUploadDone();
        }
        QMetaObject::invokeMethod(this, "handleDocumentDone", Qt::QueuedConnection);
        return;
    }
}

//==================== SocketDocRead 类实现 ====================
QScopedPointer<SocketDocRead> SocketDocRead::m_instance(nullptr);
QMutex SocketDocRead::s_instanceMutex;

// 构造函数
SocketDocRead::SocketDocRead(QObject* parent)
    : QObject(parent)
    , m_recvState(RecvState::WaitHeader)
    , m_expectedDataLen(0)
    , m_socketThread(nullptr)
    , m_socket(nullptr)
    , m_isReleased(false)
    , m_timer(nullptr)
    , m_isDataReceiving(false)
    , m_isPendingData(false)
    , m_instanceMutex()
{
    // 初始化配置
    ClientConfigDefaults::ensureClientDefaults();
    const QString configFilePath = ClientConfigDefaults::settingsIniPath();

    // 读取配置
    QSettings settings(configFilePath, QSettings::IniFormat);
    m_hostName = settings.value("Socket/HostName").toString();
    if (m_hostName.isEmpty()) m_hostName = ClientConfigDefaults::defaultSocketHostName();
    m_port = settings.value("Socket/Port").toUInt();
    if (m_port == 0) m_port = ClientConfigDefaults::defaultSocketPort();
}

// 析构函数
SocketDocRead::~SocketDocRead()
{
    qDebug() << "SocketDocRead 析构";
}

// 获取单例实例（线程安全）
SocketDocRead& SocketDocRead::instance()
{
    QMutexLocker lock(&s_instanceMutex);
    if (m_instance.isNull() || m_instance->m_isReleased) {
        m_instance.reset(new SocketDocRead());
    }
    return *m_instance;
}

// 线程安全发送数据
void SocketDocRead::scheduleDownloadSessionFinish()
{
    if (m_isReleased) return;
    QMetaObject::invokeMethod(this, "handleDataReceivedFinished", Qt::QueuedConnection);
}

void SocketDocRead::sendData(const QByteArray& data)
{
    if (m_isReleased) {
        qWarning() << "SocketDocRead instance has been released, send failed";
        return;
    }

    m_isDataReceiving = true;

    {
        QMutexLocker lock(&m_instanceMutex);
        if (m_isReleased) return;
        if (!m_socketThread) {
            qDebug() << "数据接收请求，创建SocketDocRead线程";
            m_socketThread = new QThread(nullptr);
            m_socketThread->setObjectName("SocketThread2");
            connect(m_socketThread, &QThread::finished, m_socketThread, &QThread::deleteLater);

            this->moveToThread(m_socketThread);
            m_socketThread->start();
        }
    }

    if (m_socketThread == QThread::currentThread()) {
        executeSocketOp(data, false);
    } else {
        QMetaObject::invokeMethod(this, "executeSocketOp",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data),
                                  Q_ARG(bool, false));
    }
}

// 手动释放单例资源 释放后需要重新获取实例（线程安全）
void SocketDocRead::releaseInstance()
{
    QMutexLocker lock(&s_instanceMutex);
    if (m_instance.isNull()) {
        qDebug() << "SocketDocRead already released, skip";
        return;
    }

    SocketDocRead* inst = m_instance.data();
    qDebug() << "开始清理SocketDocRead (线程状态:" << (inst->m_socketThread ? "存在" : "不存在") << ")";

    // 在 socket 线程内执行资源清理（主线程退出时通过 BlockingQueuedConnection 确保执行）
    if (inst->m_socketThread && inst->m_socketThread->isRunning()) {
        if (QThread::currentThread() == inst->m_socketThread) {
            inst->cleanupSocketResources();
        } else {
            QMetaObject::invokeMethod(inst, "cleanupSocketResources", Qt::BlockingQueuedConnection);
        }
    }

    //重置状态变量（加锁防止与 sendData 创建线程竞争）
    {
        QMutexLocker instLock(&inst->m_instanceMutex);
        inst->m_isDataReceiving = false;
        inst->m_jsonDocData.clear();
        inst->m_recvState = RecvState::WaitHeader;
        inst->m_expectedDataLen = 0;
        inst->m_isReleased = true;
    }

    //线程退出逻辑：异步quit，不等待
    if (inst->m_socketThread) {
        if (inst->m_socketThread->isRunning()) {
            QMetaObject::invokeMethod(inst->m_socketThread, "quit", Qt::QueuedConnection);
        }
        inst->m_socketThread->setParent(nullptr);
        inst->m_socketThread = nullptr; //置空，线程finished后自动deleteLater
    }

    //移回主线程并重置单例
    inst->moveToThread(QCoreApplication::instance()->thread());
    m_instance.reset();
    qDebug() << "SocketDocRead清理完成";
}

// 执行套接字操作 跨线程调用接口
void SocketDocRead::executeSocketOp(const QByteArray& data, bool isPending)
{
    Q_UNUSED(isPending);
    if (m_isReleased) return;

    //Socket未连接/断开，重建连接
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        //清理旧Socket
        if (m_socket) {
            m_socket->abort();
            m_socket->deleteLater();
            m_socket = nullptr;
        }

        //创建新Socket并配置
        m_socket = new QTcpSocket();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); //禁用Nagle算法，降低延迟

        //连接Socket信号槽
        connect(m_socket, &QTcpSocket::connected, this, &SocketDocRead::socketConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &SocketDocRead::socketDisconnected);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError err) {
            Q_UNUSED(err);
            emit socketError(m_socket->errorString());
            m_isPendingData = false;
            m_pendingData = PendingData();
            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
        });

        //连接成功后发送缓存数据
        connect(m_socket, &QTcpSocket::connected, this, &SocketDocRead::sendPendingData, Qt::DirectConnection);

        //处理接收数据（协议解析）
        connect(m_socket, &QTcpSocket::readyRead, this, [=]() {
            QByteArray recvData = m_socket->readAll();
            if (recvData.isEmpty()) return;

            m_jsonDocData += recvData;

            //循环解析完整数据包
            while (!m_jsonDocData.isEmpty() && !m_isReleased) {
                bool processed = false;
                switch (m_recvState) {
                case RecvState::WaitHeader: {
                    //等待协议头（至少8字节）
                    if (m_jsonDocData.length() < PROTOCOL_HEADER_LEN) {
                        processed = false;
                        break;
                    }

                    //解析协议头
                    ProtocolHeader header = bytesToHeader(m_jsonDocData.left(PROTOCOL_HEADER_LEN));
                    if (header.version != PROTOCOL_WIRE_VERSION) {
                        qWarning() << "不支持的协议版本：" << static_cast<int>(header.version) << "，清空缓冲区";
                        m_jsonDocData.clear();
                        m_recvState = RecvState::WaitHeader;
                        m_expectedDataLen = 0;
                        processed = false;
                        break;
                    }

                    //验证数据长度
                    m_expectedDataLen = header.dataLen;
                    m_expectedPayloadType = header.reserved[0];
                    if (m_expectedDataLen == 0 || m_expectedDataLen > MAX_PACKET_SIZE) {
                        qWarning() << "非法数据长度：" << m_expectedDataLen;
                        m_jsonDocData.clear();
                        m_recvState = RecvState::WaitHeader;
                        m_expectedDataLen = 0;
                        m_expectedPayloadType = PAYLOAD_JSON;
                        processed = false;
                        break;
                    }

                    //移除协议头，切换到等待数据状态
                    m_jsonDocData = m_jsonDocData.mid(PROTOCOL_HEADER_LEN);
                    m_recvState = RecvState::WaitData;
                    processed = true;
                    break;
                }

                case RecvState::WaitData: {
                    //等待完整数据
                    if (m_jsonDocData.length() < m_expectedDataLen) {
                        processed = false;
                        break;
                    }

                    //提取完整数据
                    QByteArray completeData = m_jsonDocData.left(m_expectedDataLen);
                    m_jsonDocData = m_jsonDocData.mid(m_expectedDataLen);

                    // 直接投递信号：上层已使用 Qt::QueuedConnection 进入主线程，
                    // 额外的 singleShot 延迟会导致 begin/end 与 binary 分片乱序，从而丢分片。
                    const uint8_t payloadType = m_expectedPayloadType; // 捕获当前包类型，避免后续重置影响判断
                    const bool isBinary = (payloadType == PAYLOAD_BINARY);
                    // 必须用单一信号投递主线程：binaryReceived 与 dataReceived 在 QueuedConnection 下不保证相对顺序
                    emit docStreamPacket(completeData, isBinary);
                    // document_end / document_error 的套接字收尾改由 MainWindow 在处理完后调用 scheduleDownloadSessionFinish()

                    //重置状态，等待下一个数据包
                    m_recvState = RecvState::WaitHeader;
                    m_expectedDataLen = 0;
                    m_expectedPayloadType = PAYLOAD_JSON;
                    processed = true;
                    break;
                }
                }

                if (!processed) {
                    break; //数据不完整，退出循环等待后续数据
                }
            }
        }, Qt::DirectConnection);

        //连接服务器
        m_socket->connectToHost(m_hostName, m_port);
        qDebug() << "SocketDocRead 重建Socket并连接服务器";

        //子线程内创建并启动心跳定时器
        if (!m_timer) {
            m_timer = new QTimer(this);
            m_timer->setInterval(35 * 1000); //35秒心跳间隔
            m_timer->setSingleShot(false);
            connect(m_timer, &QTimer::timeout, this, [=]() {
                bool connected = false;
                if (m_socket && !m_isReleased && QThread::currentThread() == m_socketThread) {
                    connected = (m_socket->state() == QAbstractSocket::ConnectedState);
                }
                if (connected) {
                    //发送心跳包
                    QJsonObject jsonObj;
                    jsonObj["tag"] = "heart";
                    QJsonDocument jsonDoc(jsonObj);
                    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
                    sendData(jsonData);
                }
            }, Qt::DirectConnection);
        }
        m_timer->start();

        //缓存数据，等待连接成功后发送
        m_pendingData = {data};
        m_isPendingData = true;
        return;
    }

    //Socket已连接，直接发送数据
    ProtocolHeader header;
    header.dataLen = static_cast<uint32_t>(data.length());
    QByteArray headerBytes = headerToBytes(header);

    //发送协议头
    const qint64 headerWriteLen = m_socket->write(headerBytes);
    if (headerWriteLen != PROTOCOL_HEADER_LEN) {
        qWarning() << "SocketDocRead 8字节协议头发送失败，实际发送：" << headerWriteLen << "字节";
        return;
    }
    m_socket->write(data);
}

// 清理套接字相关资源 关闭连接释放对象并重置状态
void SocketDocRead::cleanupSocketResources()
{
    qDebug() << "执行SocketDocRead资源清理";

    // 停止定时器
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    // 关闭套接字
    if (m_socket) {
        m_socket->disconnect();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_isPendingData = false;
    m_pendingData = PendingData();
    m_jsonDocData.clear();
    m_recvState = RecvState::WaitHeader;
    m_expectedDataLen = 0;
    m_isDataReceiving = false;
    m_isReleased = true;
}

// 处理数据接收完成 数据解析并发送信号后断开连接并清理状态
void SocketDocRead::handleDataReceivedFinished()
{
    qDebug() << "数据解析并发送完成，断开Socket连接";
    m_isDataReceiving = false;

    // 断开套接字
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    // 停止定时器
    if (m_timer) {
        m_timer->stop();
        m_timer = nullptr;
    }

    if (m_socketThread && m_socketThread->isRunning()) {
        QMetaObject::invokeMethod(this, "releaseInstance", Qt::QueuedConnection);
    } else {
        releaseInstance();
    }
}

// 发送缓存的数据
void SocketDocRead::sendPendingData()
{
    if (m_isReleased || !m_isPendingData || m_pendingData.data.isEmpty() || !m_socket) {
        return;
    }

    qDebug() << "SocketDocRead连接成功，发送缓存数据";

    // 构造协议头
    ProtocolHeader header;
    header.dataLen = static_cast<uint32_t>(m_pendingData.data.length());
    QByteArray headerBytes = headerToBytes(header);

    // 发送：QTcpSocket 自带缓冲与异步写，避免循环分块 + 每块 flush 造成抖动
    const qint64 headerWriteLen = m_socket->write(headerBytes);
    if (headerWriteLen != PROTOCOL_HEADER_LEN) {
        qWarning() << "SocketDocRead 8字节协议头发送失败，实际发送：" << headerWriteLen << "字节";
        m_isPendingData = false;
        m_pendingData = PendingData();
        return;
    }
    m_socket->write(m_pendingData.data);

    m_isPendingData = false;
    m_pendingData = PendingData();
    qDebug() << "SocketDocRead缓存数据发送完成";
}
