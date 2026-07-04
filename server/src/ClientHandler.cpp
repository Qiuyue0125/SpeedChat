/**
 * @file ClientHandler.cpp
 * 单连接协议处理、JSON 路由、动作用队列与套接字读写时间片。
 */
#include "ClientHandler.h"
#include "ClientHandlerShared.h"
#include "ConnectionPool.h"
#include "ServerCore.h"
#include "ThreadPool.h"
#include "SocketDepend.h"
#include "ServerConfigDefaults.h"
#include <QCoreApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QJsonDocument>
#include <utility>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSharedPointer>
#include <QThread>
#include <QUuid>
#include <QStringList>
#include <QCache>
#include <QMutex>
#include <QTimer>
#include <atomic>
#include <mutex>

// ===== ClientHandler 专用常量 =====
namespace {
constexpr const char *kActionTypeSendSelf = "send_self";
constexpr const char *kActionTypeSendOther = "send_other";
constexpr const char *kActionTypeClose = "close";
constexpr const char *kActionTypeAck = "ack";
constexpr const char *kActionTypeBindAccount = "bind_account";
constexpr const char *kActionTypeUploadBegin = "upload_begin";
constexpr const char *kActionTypeUploadChunk = "upload_chunk";
constexpr const char *kActionTypeUploadEnd = "upload_end";

// 噪声/高频日志采样：0=关闭，1=全量，N=每 N 次采样一次。
inline bool shouldLogNoisy(std::atomic<quint64> &counter)
{
    const int every = ServerConfigDefaults::logNoisyLogSampleEvery();
    if (every <= 0) return false;
    if (every == 1) return true;
    const quint64 v = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return (v % static_cast<quint64>(every)) == 0;
}
}

// ===== 生命周期 =====
// 构造连接处理器
ClientHandler::ClientHandler(QTcpSocket *socket, ServerCore *core, int idleTimeoutMs, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_core(core)
{
    static std::once_flag registerMetaTypeOnce;
    std::call_once(registerMetaTypeOnce, []() {
        qRegisterMetaType<QJsonObject>("QJsonObject");
    });
    // 限制 Qt 侧读缓冲上限（默认 0=几乎无上限，易在慢消费时堆满内存）。
    // 接收状态机同一时间只重组一帧，不必按「两帧」预留；按单帧上限 + 少量余量即可，多连接时省内存明显。
    if (m_socket) {
        m_socket->setParent(this);
        const qint64 readCap = static_cast<qint64>(qMax<uint32_t>(MAX_JSON_PACKET_SIZE, MAX_BINARY_PACKET_SIZE))
                               + ServerConfigDefaults::serverSocketReadSlackBytes();
        m_socket->setReadBufferSize(readCap);
    }
    // 设置存储路径
    m_savePath = ServerConfigDefaults::storageBasePath();
    // 设置连接空闲超时（由 ServerTimers 统一检查）
    m_idleTimeoutMs = (idleTimeoutMs > 0) ? idleTimeoutMs : 60000;
    m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
}

// 将账号字符串解析为 accountId（纯数字）。失败返回 false。
bool ClientHandler::tryParseAccountId(const QString &account, quint64 &outAccountId)
{
    outAccountId = 0;
    if (account.isEmpty()) return false;
    bool ok = false;
    const quint64 v = account.toULongLong(&ok, 10);
    if (!ok || v == 0) return false;
    outAccountId = v;
    return true;
}

// 套接字是否仍处于已连接状态。
bool ClientHandler::isTcpConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

// 析构处理器并释放连接相关资源
ClientHandler::~ClientHandler()
{
    m_connectionClosed = true;
    if (m_core) {
        m_core->abandonConnectionResources(this);
    }
    abortActiveForwardSlices();
    // 释放转发切片对象池。
    for (ForwardSliceState *s : m_forwardSliceStatePool) {
        if (!s) continue;
        s->timer.stop();
        delete s;
    }
    m_forwardSliceStatePool.clear();
    purgeQueuesAndSessions();
    m_uploads.clear();
}

// 回收切片状态对象（对象池复用，减少频繁 new/delete）。
void ClientHandler::recycleForwardSliceState(ForwardSliceState *state)
{
    if (!state) return;

    m_activeForwardSlices.removeAll(state);
    state->timer.stop();
    QObject::disconnect(&state->timer, nullptr, nullptr, nullptr);
    state->targetAccountId = 0;
    state->batch.clear();
    state->batch.squeeze();
    state->index = 0;

    if (m_forwardSliceStatePool.size() < m_forwardSliceStatePoolMax) {
        m_forwardSliceStatePool.append(state);
        return;
    }
    state->deleteLater();
}

// 停止所有转发切片定时器并回收切片状态对象。
void ClientHandler::abortActiveForwardSlices()
{
    const QVector<ForwardSliceState *> copy = m_activeForwardSlices;
    m_activeForwardSlices.clear();
    for (ForwardSliceState *s : copy) {
        if (!s) continue;
        s->timer.stop();
        QObject::disconnect(&s->timer, nullptr, nullptr, nullptr);
        s->batch.clear();
        s->batch.squeeze();
        recycleForwardSliceState(s);
    }
}

// 清空各类发送/解析队列、转发批次与待处理消息，并重置接收状态机。
void ClientHandler::purgeQueuesAndSessions()
{
    m_outQueue.clear();
    m_outQueueBytes = 0;
    m_docSendQueue.clear();
    m_docSendQueueBytes = 0;
    resetDocDownloadSession();
    m_jsonParseQueue.clear();
    m_jsonParseQueueBytes = 0;
    m_jsonParseInFlight = false;
    m_binaryParseQueue.clear();
    m_binaryParseQueueBytes = 0;
    m_binaryParseInFlight = false;
    {
        QMutexLocker locker(&m_forwardBatchMutex);
        m_forwardBatchByTarget.clear();
        m_forwardBatchScheduled.clear();
    }
    m_forwardFlushScheduled = false;
    m_messageQueue.clear();
    m_isSending = false;
    resetRecvState();
    m_recvBuffer.squeeze();
}

// ===== 发送队列与限速 =====
// 文档下载分片发送与限速。
bool ClientHandler::enqueueDocPacket(const QJsonObject &obj)
{
    const QByteArray payload = ClientHandlerShared::toJsonCompactBytes(obj);
    return enqueueDocPacketEncoded(payload);
}

// 发送已编码 JSON 数据包（避免重复 toJson）。
bool ClientHandler::enqueueDocPacketEncoded(const QByteArray &payload)
{
    if (payload.isEmpty()) return false;
    if (isShuttingDownConnection()) return false;
    if (!isTcpConnected()) return false;

    // 限制队列长度和内存占用
    if (m_docSendQueue.size() >= ServerConfigDefaults::serverDocSendQueueMaxPackets() ||
        (m_docSendQueueBytes + payload.size()) > ServerConfigDefaults::serverDocSendQueueMaxBytes()) {
        qWarning() << "【连接处理】发送队列已满 丢弃数据 账号" << m_accountId;
        return false;
    }

    m_docSendQueue.enqueue(OutPacket{PAYLOAD_JSON, payload});
    m_docSendQueueBytes += payload.size();
    if (m_docLastRefillMs <= 0) {
        m_docTokens = ServerConfigDefaults::serverDocBurstBytes();
        m_docLastRefillMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (m_core) m_core->registerClientForDocSendPump(this);
    return true;
}

// 发送二进制数据入队。
bool ClientHandler::enqueueBinaryPacket(const QByteArray &body)
{
    if (body.isEmpty()) return false;
    if (isShuttingDownConnection()) return false;
    if (!isTcpConnected()) return false;
    if (m_docSendQueue.size() >= ServerConfigDefaults::serverDocSendQueueMaxPackets() ||
        (m_docSendQueueBytes + body.size()) > ServerConfigDefaults::serverDocSendQueueMaxBytes()) {
        qWarning() << "【连接处理】发送队列已满 丢弃数据 账号" << m_accountId;
        return false;
    }
    m_docSendQueue.enqueue(OutPacket{PAYLOAD_BINARY, body});
    m_docSendQueueBytes += body.size();
    if (m_docLastRefillMs <= 0) {
        m_docTokens = ServerConfigDefaults::serverDocBurstBytes();
        m_docLastRefillMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (m_core) m_core->registerClientForDocSendPump(this);
    return true;
}

bool ClientHandler::docPipelineHasRoomFor(int extraBytes) const
{
    if (extraBytes < 0) extraBytes = 0;
    const int docMaxPackets = ServerConfigDefaults::serverDocSendQueueMaxPackets();
    const int docMaxBytes = ServerConfigDefaults::serverDocSendQueueMaxBytes();
    const int outMaxPackets = ServerConfigDefaults::serverOutQueueMaxPackets();
    const int outMaxBytes = ServerConfigDefaults::serverOutQueueMaxBytes();
    const qint64 highWater = ServerConfigDefaults::serverSocketWriteHighWaterBytes();

    if (m_docSendQueue.size() + 1 >= docMaxPackets) return false;
    if (m_docSendQueueBytes + extraBytes > docMaxBytes) return false;
    if (m_outQueue.size() + 1 >= outMaxPackets) return false;
    if (m_outQueueBytes + extraBytes > outMaxBytes) return false;

    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState) {
        if (m_socket->bytesToWrite() > highWater) return false;
    }
    return true;
}

void ClientHandler::resetDocDownloadSession()
{
    m_docDownload.active = false;
    m_docDownload.uuid.clear();
    m_docDownload.uuidBin.clear();
    m_docDownload.chunkBytes = 0;
    m_docDownload.totalBytes = 0;
    m_docDownload.totalChunks = 0;
    m_docDownload.nextSeq = 0;
    m_docDownload.endEnqueued = false;
    if (m_docDownload.file.isOpen()) {
        m_docDownload.file.close();
    }
}

QByteArray ClientHandler::buildDocumentChunkBody(const QByteArray &uuidBin, qint64 seq, const QByteArray &chunk)
{
    QByteArray body;
    body.reserve(1 + 16 + 4 + 4 + chunk.size());
    body.append(char(2));
    body.append(uuidBin);
    const uint32_t seqNet = hostToNetwork32(static_cast<uint32_t>(seq));
    body.append(reinterpret_cast<const char*>(&seqNet), 4);
    const uint32_t lenNet = hostToNetwork32(static_cast<uint32_t>(chunk.size()));
    body.append(reinterpret_cast<const char*>(&lenNet), 4);
    body.append(chunk);
    return body;
}

bool ClientHandler::beginStreamingDocDownload(const QJsonObject &ready)
{
    if (isShuttingDownConnection() || !isTcpConnected()) {
        return false;
    }

    const QString uuidStr = ready.value("uuid").toString();
    const QString filePath = ready.value("file_path").toString();
    const QString filename = ready.value("filename").toString();
    const qint64 totalBytes = ready.value("total_bytes").toString().toLongLong();
    const int chunkBytes = ready.value("chunk_bytes").toInt(ServerConfigDefaults::serverDocChunkBytes());

    if (uuidStr.isEmpty() || filePath.isEmpty() || totalBytes <= 0 || chunkBytes <= 0) {
        return false;
    }

    resetDocDownloadSession();

    QUuid qu = QUuid::fromString(uuidStr);
    if (qu.isNull()) {
        qu = QUuid::fromString("{" + uuidStr + "}");
    }
    if (qu.isNull()) {
        return false;
    }

    m_docDownload.file.setFileName(filePath);
    if (!m_docDownload.file.open(QIODevice::ReadOnly)) {
        resetDocDownloadSession();
        return false;
    }

    m_docDownload.active = true;
    m_docDownload.uuid = uuidStr;
    m_docDownload.uuidBin = qu.toRfc4122();
    m_docDownload.chunkBytes = chunkBytes;
    m_docDownload.totalBytes = totalBytes;
    m_docDownload.totalChunks = (totalBytes + chunkBytes - 1) / chunkBytes;
    m_docDownload.nextSeq = 0;
    m_docDownload.endEnqueued = false;

    QJsonObject begin;
    begin["tag"] = "document_begin";
    begin["uuid"] = uuidStr;
    begin["filename"] = filename;
    begin["total_bytes"] = QString::number(totalBytes);
    begin["chunk_bytes"] = chunkBytes;
    begin["total_chunks"] = QString::number(m_docDownload.totalChunks);
    if (!enqueueDocPacket(begin)) {
        resetDocDownloadSession();
        return false;
    }

    qInfo() << "【文档下载】流式发送开始 uuid=" << uuidStr << " totalBytes=" << totalBytes
            << " totalChunks=" << m_docDownload.totalChunks << " path=" << filePath;
    return true;
}

void ClientHandler::pumpDocDownloadSource(QElapsedTimer &sliceTimer)
{
    if (!m_docDownload.active || isShuttingDownConnection() || !m_docDownload.file.isOpen()) {
        return;
    }

    const int frameOverhead = 1 + 16 + 4 + 4;
    const int reserveBytes = frameOverhead + m_docDownload.chunkBytes;

    while (!m_docDownload.file.atEnd()) {
        if (sliceTimer.elapsed() >= ServerConfigDefaults::serverDocSendMaxMsPerSlice()) {
            break;
        }
        if (!docPipelineHasRoomFor(reserveBytes)) {
            break;
        }

        const QByteArray chunk = m_docDownload.file.read(m_docDownload.chunkBytes);
        if (chunk.isEmpty()) {
            break;
        }

        const QByteArray body = buildDocumentChunkBody(m_docDownload.uuidBin, m_docDownload.nextSeq, chunk);
        if (!enqueueBinaryPacket(body)) {
            break;
        }
        ++m_docDownload.nextSeq;
    }

    if (!m_docDownload.active || m_docDownload.endEnqueued) {
        return;
    }
    if (!m_docDownload.file.atEnd()) {
        return;
    }

    const int endReserve = 512;
    if (!docPipelineHasRoomFor(endReserve)) {
        return;
    }

    QJsonObject end;
    end["tag"] = "document_end";
    end["uuid"] = m_docDownload.uuid;
    end["total_bytes"] = QString::number(m_docDownload.totalBytes);
    end["total_chunks"] = QString::number(m_docDownload.totalChunks);
    if (!enqueueDocPacket(end)) {
        return;
    }

    m_docDownload.endEnqueued = true;
    m_docDownload.active = false;
    m_docDownload.file.close();
    qInfo() << "【文档下载】流式发送已入队 document_end uuid=" << m_docDownload.uuid;
}

void ClientHandler::handleDocDownloadDbResult(const QByteArray &result, const QString &uuidStr)
{
    if (result.isEmpty()) {
        QJsonObject err;
        err["tag"] = "document_error";
        err["uuid"] = uuidStr;
        err["message"] = "线程池已满，任务被拒绝";
        enqueueDocPacket(err);
        return;
    }

    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(result, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject err;
        err["tag"] = "document_error";
        err["uuid"] = uuidStr;
        err["message"] = "服务器响应异常";
        enqueueDocPacket(err);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tag = obj.value("tag").toString();
    if (tag == QLatin1String("_doc_download_ready")) {
        if (!beginStreamingDocDownload(obj)) {
            QJsonObject err;
            err["tag"] = "document_error";
            err["uuid"] = uuidStr;
            err["message"] = "failed to start download stream";
            enqueueDocPacket(err);
        }
        return;
    }

    if (tag == QLatin1String("document_error")) {
        enqueueDocPacket(obj);
        return;
    }

    QJsonObject err;
    err["tag"] = "document_error";
    err["uuid"] = uuidStr;
    err["message"] = "服务器响应异常";
    enqueueDocPacket(err);
}

// 发送队列数据（由 ServerTimers 统一调用）。
void ClientHandler::pumpDocSend()
{
    if (isShuttingDownConnection()) {
        m_docSendQueue.clear();
        m_docSendQueueBytes = 0;
        resetDocDownloadSession();
        if (m_core) m_core->unregisterClientForDocSendPump(this);
        return;
    }
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        m_docSendQueue.clear();
        m_docSendQueueBytes = 0;
        resetDocDownloadSession();
        if (m_core) m_core->unregisterClientForDocSendPump(this);
        return;
    }

    const bool rateLimitEnabled = ServerConfigDefaults::serverDocRateLimitEnabled();
    if (rateLimitEnabled) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_docLastRefillMs <= 0) m_docLastRefillMs = nowMs;
        const qint64 deltaMs = qMax<qint64>(0, nowMs - m_docLastRefillMs);
        if (deltaMs > 0) {
            const qint64 add = (static_cast<qint64>(ServerConfigDefaults::serverDocRateBytesPerSec()) * deltaMs) / 1000;
            m_docTokens = qMin<qint64>(ServerConfigDefaults::serverDocBurstBytes(), m_docTokens + add);
            m_docLastRefillMs = nowMs;
        }
    }

    QElapsedTimer sliceTimer;
    sliceTimer.start();
    pumpDocDownloadSource(sliceTimer);

    while (!m_docSendQueue.isEmpty()) {
        const OutPacket &next = m_docSendQueue.head();
        const qint64 need = next.body.size();
        if (need <= 0) {
            m_docSendQueueBytes = qMax(0, m_docSendQueueBytes - next.body.size());
            m_docSendQueue.dequeue();
            continue;
        }
        if (rateLimitEnabled && m_docTokens < need) break;

        if (!enqueueSocketPacket(next.body, /*droppable*/false, next.payloadType)) {
            break;
        }
        if (rateLimitEnabled) {
            m_docTokens -= need;
        }
        m_docSendQueue.dequeue();
        m_docSendQueueBytes -= need;
        if (sliceTimer.elapsed() >= ServerConfigDefaults::serverDocSendMaxMsPerSlice()) break;
    }
    if (m_docDownload.active) {
        pumpDocDownloadSource(sliceTimer);
    }
    if (m_docSendQueue.isEmpty() && !m_docDownload.active && m_core) m_core->unregisterClientForDocSendPump(this);
}

// 添加在线客户端。
void ClientHandler::addClient(quint64 accountId, ClientHandler *client)
{
    ServerCore *core = m_core;
    if (!core || core->isShuttingDown()) {
        qWarning() << "Server不可用或正在关闭";
        return;
    }
    core->addClient(accountId, client);
}

// 移除在线客户端。
void ClientHandler::removeClient(quint64 accountId)
{
    ServerCore *core = m_core;
    if (!core || core->isShuttingDown()) {
        qWarning() << "Server不可用或正在关闭，无法移除客户端:" << accountId;
        return;
    }
    QPointer<ClientHandler> selfPtr(this);
    core->removeClient(accountId, selfPtr);
}

// 获取在线客户端。
ClientHandler* ClientHandler::getClient(quint64 accountId)
{
    ServerCore *core = m_core;
    if (!core || core->isShuttingDown()) {
        qWarning() << "Server不可用或正在关闭";
        return nullptr;
    }
    return core->getClient(accountId);
}

// 重置连接空闲超时计时器
void ClientHandler::resetTimeoutTimer()
{
    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState) {
        m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
    }
}

// 由 ServerTimers 调用：检查空闲超时并断开
void ClientHandler::checkIdleTimeout()
{
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastActivityMs > 0 && (now - m_lastActivityMs) >= m_idleTimeoutMs) {
        qInfo() << "客户端超时断联，账号:" << m_accountId << "对端:" << m_socket->peerAddress().toString() << ":" << m_socket->peerPort();
        m_socket->disconnectFromHost();
    }
}

// 由 ServerTimers 调用：清理超时上传会话
void ClientHandler::checkUploadCleanup()
{
    if (m_uploads.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QString> toRemove;
    for (auto it = m_uploads.begin(); it != m_uploads.end(); ++it) {
        const UploadSession &s = it.value();
        if (!s.active) {
            toRemove.append(it.key());
            continue;
        }
        if (s.lastActivityMs > 0 && (now - s.lastActivityMs) > ServerConfigDefaults::defaultUploadIdleTimeoutMs()) {
            toRemove.append(it.key());
        }
    }
    for (const QString &uuid : toRemove) {
        auto it = m_uploads.find(uuid);
        if (it == m_uploads.end()) continue;
        UploadSession &s = it.value();
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, s.filePath);
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "上传超时";
        enqueueDocPacket(err);
        m_uploads.erase(it);
    }
    updateUploadCleanupRegistration();
}

// 根据 m_uploads 状态更新 upload cleanup 注册。
void ClientHandler::updateUploadCleanupRegistration()
{
    if (!m_core) return;
    if (m_uploads.isEmpty())
        m_core->unregisterClientForUploadCleanup(this);
    else
        m_core->registerClientForUploadCleanup(this);
}

// 发送数据到套接字
void ClientHandler::socketWrite(const QByteArray& data)
{
    if (isShuttingDownConnection()) return;
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        qWarning() << "Socket未连接，无法发送数据";
        return;
    }
    enqueueSocketPacket(data, false, PAYLOAD_JSON);
}

// AI 分析结束：清除进行中标记并回包（须在连接所属线程调用）。
void ClientHandler::postAiAnalyzeResult(const QJsonObject &out)
{
    m_aiAnalyzeInFlight = false;
    socketWrite(ClientHandlerShared::toJsonCompactBytes(out));
}

// 发送低优先级数据到套接字（队列满可丢弃）
void ClientHandler::socketWriteLowPriority(const QByteArray &data)
{
    if (isShuttingDownConnection()) return;
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        return;
    }
    enqueueSocketPacket(data, true, PAYLOAD_JSON);
}

// 头体分离入队（背压）
bool ClientHandler::enqueueSocketPacket(const QByteArray &body, bool droppable, uint8_t payloadType)
{
    if (body.isEmpty()) return false;
    if (isShuttingDownConnection()) return false;
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) return false;
    const QByteArray header = makeHeaderBytes(static_cast<uint32_t>(body.size()), payloadType);
    const int packetBytes = header.size() + body.size();

    if (m_outQueue.size() >= ServerConfigDefaults::serverOutQueueMaxPackets() ||
        (m_outQueueBytes + packetBytes) > ServerConfigDefaults::serverOutQueueMaxBytes()) {
        static std::atomic<quint64> sOutQueueFullLogCounter{0};
        if (droppable) {
            if (ClientHandlerShared::shouldSample(sOutQueueFullLogCounter)) {
                qWarning() << "【连接处理】原始发送队列已满，丢弃低优先级包 账号" << m_accountId;
            }
            return false;
        }
        if (m_docDownload.active || !m_docSendQueue.isEmpty()) {
            if (ClientHandlerShared::shouldSample(sOutQueueFullLogCounter)) {
                qWarning() << "【连接处理】原始发送队列已满，文档传输背压等待 账号" << m_accountId;
            }
            return false;
        }
        if (ClientHandlerShared::shouldSample(sOutQueueFullLogCounter)) {
            qWarning() << "【连接处理】原始发送队列已满，执行背压断开 账号" << m_accountId;
        }
        m_socket->disconnectFromHost();
        return false;
    }

    m_outQueue.enqueue(RawOutPacket{header, body, 0, 0, droppable});
    m_outQueueBytes += packetBytes;
    ClientHandlerShared::emitPerfMetric("out_queue_bytes", m_outQueueBytes);
    pumpSocketWrite();
    return true;
}

// 尝试发送原始队列
void ClientHandler::pumpSocketWrite()
{
    // 如果连接正在关闭：清空发送队列，直接返回
    if (isShuttingDownConnection()) {
        m_outQueue.clear();        // 清空待发送数据包队列
        m_outQueueBytes = 0;       // 重置待发送字节数统计
        return;
    }

    // 如果 Socket 无效 或 未处于连接状态：清空队列并返回
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        m_outQueue.clear();
        m_outQueueBytes = 0;
        return;
    }

    // 获取发送高水位线（控制发送速度，防止内核缓冲区溢出）
    const qint64 highWater = ServerConfigDefaults::serverSocketWriteHighWaterBytes();

    // 循环发送：只要队列不为空就一直尝试发送
    while (!m_outQueue.isEmpty()) {
        // 如果内核发送缓冲区积压数据超过高水位线，暂停发送，避免拥堵
        if (m_socket->bytesToWrite() > highWater) {
            break;
        }

        // 获取队列头部第一个待发送包（header + body）
        RawOutPacket &front = m_outQueue.head();
        const qint64 packetSize = front.header.size() + front.body.size();

        // ===================== 发送包头 =====================
        if (front.headerSent < front.header.size()) {
            // 计算包头剩余未发送字节数
            const qint64 remainHeader = front.header.size() - front.headerSent;
            // 非阻塞写入 Socket
            const qint64 writtenHeader = m_socket->write(front.header.constData() + front.headerSent, remainHeader);

            // 写不进去（缓冲区满/出错），退出循环，下次再试
            if (writtenHeader <= 0) {
                break;
            }

            // 更新已发送的包头字节数
            front.headerSent += writtenHeader;
            // 刷新超时计时器：发送数据也算活跃，避免长连接单向传输时被误判超时断开
            resetTimeoutTimer();

            // 包头没发完，退出循环，下次继续发剩下的
            if (front.headerSent < front.header.size()) {
                break;
            }
        }

        // ===================== 发送包体 =====================
        if (front.bodySent < front.body.size()) {
            // 计算包体剩余未发送字节数
            const qint64 remainBody = front.body.size() - front.bodySent;
            // 非阻塞写入 Socket
            const qint64 writtenBody = m_socket->write(front.body.constData() + front.bodySent, remainBody);

            // 写不进去，退出循环
            if (writtenBody <= 0) {
                break;
            }

            // 更新已发送的包体字节数
            front.bodySent += writtenBody;
            // 刷新超时计时器
            resetTimeoutTimer();

            // 包体没发完，退出循环，下次继续
            if (front.bodySent < front.body.size()) {
                break;
            }
        }

        // ===================== 整包发送完成 =====================
        if (front.headerSent >= front.header.size() && front.bodySent >= front.body.size()) {
            m_outQueueBytes -= packetSize;   // 减去已发送的字节数
            m_outQueue.dequeue();             // 从发送队列移除该包
        }
    }
}

// 接收协议头
bool ClientHandler::receiveHeader()
{
    if (isShuttingDownConnection()) return false;
    qint64 needRead = PROTOCOL_HEADER_LEN - m_recvBuffer.length();
    QByteArray tempBuf = m_socket->read(needRead); // 安全读取到临时缓冲区
    qint64 readLen = tempBuf.length();

    if (readLen <= 0) { // 读取失败
        qWarning() << "【连接处理】协议头读取失败 长度" << readLen
                   << "，错误：" << m_socket->errorString();
        m_recvBuffer.clear();
        m_recvBuffer.squeeze();
        return false;
    }

    m_recvBuffer.append(tempBuf);

    if (m_recvBuffer.length() < PROTOCOL_HEADER_LEN) {
        static std::atomic<quint64> sPartialHeaderNoisyCounter{0};
        if (shouldLogNoisy(sPartialHeaderNoisyCounter)) {
            qDebug() << "【连接处理】协议头未收完 当前长度" << m_recvBuffer.length();
        }
        return false;
    }

    ProtocolHeader header = bytesToHeader(m_recvBuffer);
    m_recvBuffer.clear();
    m_recvBuffer.squeeze();

    // 版本号
    if (header.version != PROTOCOL_WIRE_VERSION) {
        qWarning() << "【连接处理】协议版本不支持 断开连接" << static_cast<int>(header.version);
        resetRecvState();
        m_socket->disconnectFromHost();
        return false;
    }

    // 校验数据长度
    m_expectedDataLen = header.dataLen;
    m_expectedPayloadType = header.reserved[0];
    const uint32_t maxPayload = (m_expectedPayloadType == PAYLOAD_BINARY)
        ? MAX_BINARY_PACKET_SIZE
        : MAX_JSON_PACKET_SIZE;
    if (m_expectedDataLen == 0 || m_expectedDataLen > maxPayload) {
        resetRecvState();
        m_socket->disconnectFromHost();
        return false;
    }

    m_recvState = RecvState::WaitData;
    return true;
}

// 接收协议体
bool ClientHandler::receiveData()
{
    if (isShuttingDownConnection()) return false;
    if (m_expectedDataLen == 0) {
        qWarning() << "【连接处理】接收状态异常 已重置";
        resetRecvState();
        return false;
    }

    // 长度超限校验
    const uint32_t maxPayload = (m_expectedPayloadType == PAYLOAD_BINARY)
        ? MAX_BINARY_PACKET_SIZE
        : MAX_JSON_PACKET_SIZE;
    if (m_expectedDataLen > maxPayload) {
        qWarning() << "【连接处理】数据长度超过限制" << m_expectedDataLen;
        resetRecvState();
        m_socket->disconnectFromHost();
        return false;
    }

    qint64 needRead = m_expectedDataLen - m_recvBuffer.length();
    QByteArray tempBuf = m_socket->read(needRead);
    qint64 readLen = tempBuf.length();

    if (readLen <= 0) {
        qWarning() << "【连接处理】数据体读取失败 长度" << readLen
                   << "，错误：" << m_socket->errorString();
        return false;
    }

    m_recvBuffer.append(tempBuf);

    if (m_recvBuffer.length() < m_expectedDataLen) {
        static std::atomic<quint64> sPartialBodyNoisyCounter{0};
        if (shouldLogNoisy(sPartialBodyNoisyCounter)) {
            qDebug() << "【连接处理】数据体未收完 当前长度" << m_recvBuffer.length();
        }
        return false;
    }

    if (m_expectedPayloadType == PAYLOAD_BINARY) {
        enqueueBinaryParsePacket(m_recvBuffer);
    } else {
        enqueueJsonParsePacket(m_recvBuffer);
    }

    resetRecvState();
    return true;
}

// 重置接收状态
void ClientHandler::resetRecvState()
{
    m_recvBuffer.clear();
    m_recvBuffer.squeeze();
    m_expectedDataLen = 0;
    m_expectedPayloadType = PAYLOAD_JSON;
    m_recvState = RecvState::WaitHeader;
}

// 处理上传分片二进制数据
void ClientHandler::handleUploadChunkBytes(const QString &uuid, qint64 seq, const QByteArray &bin)
{
    if (uuid.isEmpty() || !m_uploads.contains(uuid)) {
        // 上传会话不存在
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "上传会话不存在";
        enqueueDocPacket(err);  // 把错误消息发送给客户端
        return;
    }

    // 获取对应的上传会话
    UploadSession &s = m_uploads[uuid];
    // 更新会话最后活跃时间
    s.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

    // 分片数据不能为空
    if (bin.isEmpty()) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "分片为空";
        enqueueDocPacket(err);
        return;
    }

    // 分片序号必须严格按顺序
    // 当前是严格顺序上传，不支持乱序/续传
    if (seq != s.nextSeq) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "分片序号不匹配";
        err["expected_seq"] = QString::number(s.nextSeq);  // 期望的序号
        err["seq"] = QString::number(seq);                // 实际收到的序号
        enqueueDocPacket(err);

        // 序号错误 → 上传失败，清理文件 + 移除会话
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, s.filePath);
        m_uploads.remove(uuid);
        updateUploadCleanupRegistration();  // 更新超时清理列表
        return;
    }

    // 数据大小不能超过文件总大小（防溢出）
    // 已接收 + 待写入 + 当前分片 不能超过文件总大小
    const qint64 queuedTotal = s.receivedBytes + s.pendingBytes + bin.size();
    if (queuedTotal > s.totalBytes) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "分片超出总大小";
        enqueueDocPacket(err);

        // 溢出 → 上传失败，清理资源
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, s.filePath);
        m_uploads.remove(uuid);
        updateUploadCleanupRegistration();
        return;
    }

    // 文件保存路径必须有效
    if (s.filePath.isEmpty()) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "upload session file path invalid";
        enqueueDocPacket(err);

        // 路径无效 → 移除会话
        m_uploads.remove(uuid);
        updateUploadCleanupRegistration();
        return;
    }

    // 校验全部通过 → 准备接收下一个分片
    s.nextSeq += 1;  // 下一个期望的分片序号 +1

    // 加入待写入磁盘的队列
    s.pendingChunks.enqueue(UploadSession::PendingChunk{seq, bin});  // 分片入队
    s.pendingBytes += bin.size();                                    // 累计待写入字节数

    // 调度异步写入磁盘  线程池写入，不阻塞网络线程
    dispatchUploadChunkWrite(uuid);
}

// 调度上传分片异步写盘
void ClientHandler::dispatchUploadChunkWrite(const QString &uuid)
{
    // uuid无效 或 上传会话不存在 → 直接返回
    if (uuid.isEmpty() || !m_uploads.contains(uuid))
        return;

    // 获取上传会话
    UploadSession &s = m_uploads[uuid];

    // 已有写入任务在执行 或 没有待写入分片 → 直接返回
    if (s.writeInFlight || s.pendingChunks.isEmpty())
        return;

    // ===================== 批量聚合写入 =====================
    // 一次写一批，减少磁盘IO次数
    QList<UploadSession::PendingChunk> batch;
    // 预分配内存，避免频繁扩容
    batch.reserve(ServerConfigDefaults::defaultUploadWriteBatchMaxChunks());

    int batchBytes = 0; // 当前批次总字节数

    // 循环取分片，凑够一个批次：
    // 条件1 有待写数据
    // 条件2 未达到最大分片数
    while (!s.pendingChunks.isEmpty() && batch.size() < ServerConfigDefaults::defaultUploadWriteBatchMaxChunks())
    {
        // 查看队首分片（不取出）
        const UploadSession::PendingChunk chunk = s.pendingChunks.head();

        // 如果加入该片会超过单批最大字节 → 停止批量收集
        if (!batch.isEmpty() && (batchBytes + chunk.data.size()) > ServerConfigDefaults::defaultUploadWriteBatchMaxBytes()) {
            break;
        }

        // 加入批次，并从待写队列移除
        batch.append(s.pendingChunks.dequeue());
        batchBytes += chunk.data.size();
    }

    // 批次为空，无需写入
    if (batch.isEmpty())
        return;

    // 更新待写字节数（减去本次批量写入的大小）
    s.pendingBytes = qMax(0, s.pendingBytes - batchBytes);

    // 正在写入磁盘（防止并发写同一个文件）
    s.writeInFlight = true;

    // ===================== 准备回调需要的数据 =====================
    // 只把需要的序号、大小传回主线程，不用传完整大数据
    QList<qint64> batchSeqs;
    QList<int> batchSizes;
    batchSeqs.reserve(batch.size());
    batchSizes.reserve(batch.size());

    for (const UploadSession::PendingChunk &item : batch) {
        batchSeqs.append(item.seq);    // 记录分片序号（用于ACK）
        batchSizes.append(item.data.size()); // 记录分片大小
    }

    QPointer<ClientHandler> thisPtr(this);

    // 标记写入是否成功
    auto writeOk = QSharedPointer<bool>::create(false);

    // ===================== 丢到线程池异步写入磁盘 =====================
    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,

        // ------------------ 线程池执行：磁盘写入（耗时操作） ------------------
        [filePath = s.filePath, batch, writeOk]() {
            // 追加模式，不覆盖原有内容
            QFile out(filePath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
                return; // 打开失败，直接返回
            }

            // 循环写入本批次所有分片
            for (const UploadSession::PendingChunk &item : batch) {
                const qint64 written = out.write(item.data);
                // 写入字节数不匹配 → 写入失败
                if (written != item.data.size()) {
                    out.close();
                    return;
                }
            }

            // 所有分片写入完成
            out.close();
            *writeOk = true; // 标记成功
        },

        // ------------------ 回到主线程：结果处理 + ACK应答 ------------------
        [thisPtr, uuid, batchSeqs, batchSizes, writeOk]() {
            // 对象已销毁 / 连接关闭 / 会话不存在 → 直接返回
            if (thisPtr.isNull() || thisPtr->isShuttingDownConnection() || !thisPtr->m_uploads.contains(uuid))
                return;

            // 获取上传会话
            UploadSession &session = thisPtr->m_uploads[uuid];
            session.writeInFlight = false; // 重置写入标记
            session.lastActivityMs = QDateTime::currentMSecsSinceEpoch(); // 刷新活跃时间

            // 写入失败 → 结束上传，返回错误
            if (!*writeOk) {
                thisPtr->finishUploadSession(uuid, false, "write failed");
                return;
            }

            // ===================== 写入成功：给客户端发送ACK =====================
            const int n = qMin(batchSeqs.size(), batchSizes.size());
            for (int i = 0; i < n; ++i) {
                session.receivedBytes += batchSizes[i]; // 更新已接收总字节

                // 构造ACK消息：告诉客户端这片已收到并写入磁盘
                QJsonObject ack;
                ack["tag"] = "upload_ack";
                ack["uuid"] = uuid;
                ack["seq"] = static_cast<int>(batchSeqs[i]);
                ack["received_bytes"] = QString::number(session.receivedBytes);
                ack["total_bytes"] = QString::number(session.totalBytes);

                // 低优先级发送ACK（不影响业务数据）
                thisPtr->socketWriteLowPriority(ClientHandlerShared::toJsonCompactBytes(ack));
            }

            // ===================== 检查是否全部上传完成 =====================
            if (session.endRequested &&             // 客户端已发结束包
                !session.writeInFlight &&           // 无正在写入任务
                session.pendingChunks.isEmpty() &&  // 无待写分片
                session.receivedBytes == session.totalBytes) // 收到字节 = 总大小
            {
                // 上传完成！关闭会话、校验文件、通知客户端
                thisPtr->finishUploadSession(uuid, true);
                return;
            }

            // 继续调度下一批写入（串行写，保证顺序）
            thisPtr->dispatchUploadChunkWrite(uuid);
        }
        );
}

// JSON 包入队并异步解析
bool ClientHandler::enqueueJsonParsePacket(const QByteArray &packet)
{
    if (packet.isEmpty()) return false;
    if (isShuttingDownConnection()) return false;
    if (m_jsonParseQueue.size() >= ServerConfigDefaults::serverJsonParseQueueMaxPackets() ||
        (m_jsonParseQueueBytes + packet.size()) > ServerConfigDefaults::serverJsonParseQueueMaxBytes()) {
        // 更细背压：优先丢弃低优先级包（如 heart），避免在尖峰时误伤连接。
        QString tag;
        const bool gotTag = ClientHandlerShared::tryExtractJsonTagFast(packet, tag);
        bool droppable = false;
        if (gotTag) {
            droppable = ServerConfigDefaults::serverBackpressureDroppableTagSet().contains(tag);
        }
        if (droppable) {
            static std::atomic<quint64> sJsonDropNoisyCounter{0};
            if (shouldLogNoisy(sJsonDropNoisyCounter)) {
                qWarning() << "【连接处理】JSON解析队列已满，丢弃低优先级包 tag=" << tag << "账号" << m_accountId;
            }
            return false;
        }

        qWarning() << "【连接处理】JSON解析队列已满，断开连接 账号" << m_accountId << (gotTag ? (QString(" tag=") + tag) : QString());
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->disconnectFromHost();
        }
        return false;
    }

    m_jsonParseQueue.enqueue(packet);
    m_jsonParseQueueBytes += packet.size();
    dispatchNextJsonParse();
    return true;
}

// 刷新限流窗口计数（按 Settings.ini 的 RateLimitWindowMs）。
void ClientHandler::updateRateLimitWindow(qint64 nowMs)
{
    const int windowMs = ServerConfigDefaults::serverRateLimitWindowMs();
    if (windowMs <= 0) return;
    if (m_rateLimitWindowStartMs <= 0 || (nowMs - m_rateLimitWindowStartMs) >= windowMs) {
        m_rateLimitWindowStartMs = nowMs;
        m_heartCountInWindow = 0;
        m_unknownTagCountInWindow = 0;
    }
}

// 启动下一条 JSON 解析任务
void ClientHandler::dispatchNextJsonParse()
{
    // 防并发：已有解析任务在执行 或 解析队列为空 → 直接退出
    // 同一时间只允许一个批量解析任务，保证处理顺序
    if (m_jsonParseInFlight || m_jsonParseQueue.isEmpty()) {
        return;
    }

    // 正在执行批量JSON解析（防止重入）
    m_jsonParseInFlight = true;

    // ===================== 攒一批再解析，提升性能 =====================
    QVector<QByteArray> batch; // 存放本次批量解析的数据包
    // 预分配内存，避免动态扩容损耗
    batch.reserve(ServerConfigDefaults::defaultJsonParseBatchMaxPackets());

    int batchBytes = 0; // 记录本次批次总字节数

    // 循环从队列取包，凑够一个批次
    // 停止条件  队空 或 达到单批最大包数
    while (!m_jsonParseQueue.isEmpty() &&
           batch.size() < ServerConfigDefaults::defaultJsonParseBatchMaxPackets())
    {
        // 查看队首包（不取出）
        const QByteArray payload = m_jsonParseQueue.head();

        // 如果加入该包后超出单批最大字节限制，则停止收集
        if (!batch.isEmpty() &&
            (batchBytes + payload.size()) > ServerConfigDefaults::defaultJsonParseBatchMaxBytes())
        {
            break;
        }

        // 将包加入批次，并从队列移除
        batch.append(m_jsonParseQueue.dequeue());
        // 累计批次字节数
        batchBytes += payload.size();
    }

    // 更新队列总字节数
    m_jsonParseQueueBytes = qMax(0, m_jsonParseQueueBytes - batchBytes);

    // 弱指针保护当前连接对象，防止异步期间对象销毁导致崩溃
    QPointer<ClientHandler> thisPtr(this);

    // 创建共享指针容器 存放解析成功的(tag, json)对
    // 跨线程传递数据，自动管理生命周期
    auto parsed = QSharedPointer<QVector<QPair<QString, QJsonObject>>>::create();
    parsed->reserve(batch.size()); // 预分配

    // ===================== 提交到业务线程池异步解析 =====================
    // 快照 m_accountId：主线程读取后按值捕获，避免工作线程跨线程读取成员导致数据竞争
    const quint64 accountId = m_accountId;

    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,  // 关联当前连接，用于取消任务

        // ------------------ 工作线程执行解析 ------------------
        [thisPtr, batch, parsed, accountId]() {
            if (thisPtr.isNull() || thisPtr->isShuttingDownConnection())
                return;

            // 遍历批量数据，逐个解析JSON
            for (const QByteArray &payload : batch) {
                QString tag;       // 消息类型tag
                QJsonObject jsonObj; // 解析后的JSON对象

                // 调用工具函数解析（内部包含完整JSON校验）
                const bool ok = ClientHandlerShared::parseJsonPacket(payload, tag, jsonObj);

                if (!ok) {
                    // 解析失败，打印警告，跳过该包
                    qWarning() << "【连接处理】JSON解析失败，账号" << accountId;
                    continue;
                }

                // 解析成功，加入结果列表
                parsed->append(qMakePair(tag, jsonObj));
            }
        },

        // ------------------ 第二个Lambda：【主线程】处理解析结果 ------------------
        [thisPtr, parsed]() {
            if (thisPtr.isNull())
                return;

            if (thisPtr->isShuttingDownConnection()) {
                thisPtr->m_jsonParseInFlight = false;
                return;
            }

            // 遍历所有解析成功的JSON消息，逐个处理
            for (const auto &p : *parsed) {
                // 循环中再次校验对象状态，双重保险
                if (thisPtr.isNull() || thisPtr->isShuttingDownConnection())
                    return;

                // 分发到业务逻辑处理
                thisPtr->processParsedJsonObject(p.first, p.second);
            }

            // 解析处理完成，重置标记
            thisPtr->m_jsonParseInFlight = false;

            // 继续解析下一批（直到队列为空）
            thisPtr->dispatchNextJsonParse();
        }
        );
}

// 二进制包入队并异步解析
bool ClientHandler::enqueueBinaryParsePacket(const QByteArray &packet)
{
    if (packet.isEmpty()) return false;
    if (isShuttingDownConnection()) return false;
    if (m_binaryParseQueue.size() >= ServerConfigDefaults::serverBinaryParseQueueMaxPackets() ||
        (m_binaryParseQueueBytes + packet.size()) > ServerConfigDefaults::serverBinaryParseQueueMaxBytes()) {
        qWarning() << "【连接处理】二进制解析队列已满，断开连接 账号" << m_accountId;
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->disconnectFromHost();
        }
        return false;
    }

    m_binaryParseQueue.enqueue(packet);
    m_binaryParseQueueBytes += packet.size();
    dispatchNextBinaryParse();
    return true;
}

// 启动下一条二进制解析任务
void ClientHandler::dispatchNextBinaryParse()
{
    // 如果已有解析任务在执行 或 队列为空 直接退出
    if (m_binaryParseInFlight || m_binaryParseQueue.isEmpty()) {
        return;
    }

    // 正在解析二进制包（同一时间只允许一个解析任务）
    m_binaryParseInFlight = true;

    // 从队列取出待解析的
    QByteArray payload = m_binaryParseQueue.dequeue();

    // 更新队列总字节数（减去当前取出的包大小，保证统计准确）
    m_binaryParseQueueBytes = qMax(0, m_binaryParseQueueBytes - payload.size());

    QPointer<ClientHandler> thisPtr(this);

    // 定义解析结果结构体 存储解析后的所有数据
    struct ParseResult {
        bool ok = false;          // 解析是否成功
        QString uuid;             // 文件/任务唯一标识
        qint64 seq = -1;          // 分片序号
        QByteArray chunk;         // 解析出来的二进制数据块
        QString error;            // 错误信息
    };

    // 跨线程传递，自动管理生命周期
    auto result = QSharedPointer<ParseResult>::create();

    // 把解析任务丢到全局业务线程池执行（不阻塞网络IO线程）
    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,  // 绑定当前连接，任务取消时自动关联

        // ------------------ 线程池执行耗时解析逻辑 ------------------
        [payload, result]() {
            // 在线程池中执行：二进制块解析
            result->ok = ClientHandlerShared::parseBinaryChunkPacket(
                payload,
                result->uuid,
                result->seq,
                result->chunk,
                result->error
                );
        },

        // ------------------ 回到当前线程结果处理 ------------------
        [thisPtr, result]() {
            // 当前对象已销毁，直接返回
            if (thisPtr.isNull()) return;

            // 连接正在关闭，重置标记并退出
            if (thisPtr->isShuttingDownConnection()) {
                thisPtr->m_binaryParseInFlight = false;
                return;
            }

            // 解析失败 → 打印日志 + 断开连接
            if (!result->ok) {
                qWarning() << "【连接处理】二进制解析失败 账号" << thisPtr->m_accountId << "原因:" << result->error;
                if (thisPtr->m_socket && thisPtr->m_socket->state() == QAbstractSocket::ConnectedState) {
                    thisPtr->m_socket->disconnectFromHost();
                }
                thisPtr->m_binaryParseInFlight = false;   // 重置标记
                thisPtr->dispatchNextBinaryParse();       // 继续解析下一条
                return;
            }

            // 解析成功 → 处理上传的二进制分片（文件块、数据块等）
            thisPtr->handleUploadChunkBytes(result->uuid, result->seq, result->chunk);

            // 重置标记，继续调度下一个二进制包解析（串行执行）
            thisPtr->m_binaryParseInFlight = false;
            thisPtr->dispatchNextBinaryParse();
        }
        );
}

// 按 tag 分发已解析 JSON
void ClientHandler::processParsedJsonObject(const QString &tag, const QJsonObject &jsonObj)
{
    // ===================== 日志防刷屏 =====================
    // 原子计数器 避免高并发下日志疯狂输出，撑爆磁盘
    static std::atomic<quint64> sRecvTagNoisyCounter{0};
    if (shouldLogNoisy(sRecvTagNoisyCounter)) {
        qDebug() << "收到请求 tag=" << tag << "账号=" << m_accountId;
    }

    // ===================== 心跳包特殊处理 =====================
    if (tag == QStringLiteral("heart")) {
        // 当前时间（毫秒）
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        // 更新限流时间窗口（滑动窗口，固定周期重置计数）
        updateRateLimitWindow(nowMs);

        // 心跳限流：单位时间内心跳太多 → 直接丢弃
        const int maxHeart = ServerConfigDefaults::serverHeartMaxPerWindow();
        if (maxHeart >= 0 && (++m_heartCountInWindow) > maxHeart) {
            static std::atomic<quint64> sHeartLimitNoisyCounter{0};
            if (shouldLogNoisy(sHeartLimitNoisyCounter)) {
                qWarning() << "【连接处理】心跳过频，已丢弃 账号" << m_accountId;
            }
            return; // 直接丢弃，不回包、不处理
        }

        // 正常心跳：回复 pong 给客户端
        QJsonObject pong;
        pong["tag"] = "pong";
        // 发送消息
        socketWrite(ClientHandlerShared::toJsonCompactBytes(pong));

        // 心跳日志（限流输出）
        static std::atomic<quint64> sHeartNoisyCounter{0};
        if (shouldLogNoisy(sHeartNoisyCounter)) {
            qDebug() << "心跳包处理完成 tag=heart 账号=" << m_accountId;
        }
        return; // 心跳处理完毕，直接返回
    }

    // ===================== 消息路由 =====================
    using JsonHandler = void (ClientHandler::*)(const QJsonObject &);

    static const QHash<QString, JsonHandler> kRouteHandlers = {
        {"login", &ClientHandler::dealLogin},                // 登录
        {"register", &ClientHandler::dealRegister},          // 注册
        {"findpassword1", &ClientHandler::dealFindpassword1}, // 找回密码1
        {"findpassword2", &ClientHandler::dealFindpassword2},
        {"findpassword3", &ClientHandler::dealFindpassword3},
        {"askforloginmes0", &ClientHandler::dealLoginMes0},
        {"askforloginmes1", &ClientHandler::dealLoginMes1},
        {"askforloginmes2", &ClientHandler::dealLoginMes2},
        {"deletefriend", &ClientHandler::dealDeleteFriend},      // 删除好友
        {"searchfriend", &ClientHandler::dealSearchAccount},    // 搜索账号
        {"changeinformation", &ClientHandler::dealChangeInformation}, // 修改资料
        {"changepassword1", &ClientHandler::dealChangePassword},
        {"changepassword2", &ClientHandler::dealChangePassword2},
        {"logout", &ClientHandler::dealLogout},                // 登出
        {"addfriend", &ClientHandler::dealAddFriends},          // 发送好友申请
        {"newfriends", &ClientHandler::dealAddFriendsRespond}, // 处理好友申请
        {"messages", &ClientHandler::dealMessages},            // 发送聊天消息
        {"askfordocument", &ClientHandler::dealAskDocument},   // 请求文件
        {"askforcloudfile", &ClientHandler::dealAskCloudFile}, // 请求网盘文件
        {"searchcloudfile", &ClientHandler::dealSearchCloudFile}, // 查询网盘文件
        {"listmycloudfiles", &ClientHandler::dealListMyCloudFiles}, // 我的网盘文件列表
        {"upload_begin", &ClientHandler::dealUploadBegin},     // 上传开始
        {"upload_chunk", &ClientHandler::dealUploadChunk},     // 上传分片（JSON指令）
        {"upload_end", &ClientHandler::dealUploadEnd},         // 上传结束
        {"askforfriendinfor", &ClientHandler::dealAskForFriend}, // 获取好友信息
        {"messageread", &ClientHandler::dealMessageRead},      // 消息已读
        {"uploadSucceed", &ClientHandler::dealLoginMessageRead},
        {"chat_ai_analyze", &ClientHandler::dealChatAiAnalyze}, // AI 分析
    };

    // ===================== 查找对应处理函数 =====================
    const auto it = kRouteHandlers.constFind(tag);

    // ===================== 没有找到 未知 tag 处理 =====================
    if (it == kRouteHandlers.constEnd()) {
        // 恶意刷非法请求会被限制
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        updateRateLimitWindow(nowMs);
        const int maxUnknown = ServerConfigDefaults::serverUnknownTagMaxPerWindow();

        // 超过阈值 直接丢弃
        if (maxUnknown >= 0 && (++m_unknownTagCountInWindow) > maxUnknown) {
            static std::atomic<quint64> sUnknownLimitNoisyCounter{0};
            if (shouldLogNoisy(sUnknownLimitNoisyCounter)) {
                qWarning() << "【连接处理】未知tag过频 tag=" << tag << "账号" << m_accountId;
            }

            // 配置开启的话 直接断开恶意连接
            if (ServerConfigDefaults::serverDisconnectOnUnknownTagLimit()) {
                ClientHandlerShared::runOnObjectThread(this, [this]() {
                    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
                        m_socket->disconnectFromHost();
                    }
                });
            }
            return;
        }

        // 打印日志
        static std::atomic<quint64> sUnknownTagNoisyCounter{0};
        if (shouldLogNoisy(sUnknownTagNoisyCounter)) {
            qDebug() << "未知或扩展 tag，已忽略 tag=" << tag << "账号=" << m_accountId;
        }
        return;
    }

    // ===================== 找到处理函数：执行 =====================
    const JsonHandler handler = it.value();
    // 调用当前对象的成员函数
    (this->*handler)(jsonObj);
}

// 消费线程任务返回的 Action 列表
void ClientHandler::consumeWorkerActions(const WorkerActionList &actions)
{
    if (actions.isEmpty()) return;
    for (const QJsonValue &v : actions) {
        if (!v.isObject()) continue;
        executeWorkerAction(v.toObject());
    }
}

// 执行单个 Action。
void ClientHandler::executeWorkerAction(const QJsonObject &action)
{
    // 拿到这条指令的类型：发消息.踢人.关闭...
    const QString type = action.value("type").toString();

    // 定义函数指针类型：所有指令处理函数，格式都是 void func(const QJsonObject&)
    using ActionHandler = void (ClientHandler::*)(const QJsonObject &);

    // 类型字符串 → 对应的处理函数
    static const QHash<QString, ActionHandler> kActionHandlers = {
        {kActionTypeSendSelf,    &ClientHandler::handleActionSendSelf},    // 发给自己
        {kActionTypeSendOther,   &ClientHandler::handleActionSendOther},   // 发给别人
        {kActionTypeClose,       &ClientHandler::handleActionClose},       // 关闭连接
        {kActionTypeAck,         &ClientHandler::handleActionAck},         // 回复ACK
        {kActionTypeBindAccount, &ClientHandler::handleActionBindAccount}, // 登录成功，绑定账号
        {kActionTypeUploadBegin, &ClientHandler::handleActionUploadBegin}, // 开始上传
        {kActionTypeUploadChunk, &ClientHandler::handleActionUploadChunk}, // 上传分片
        {kActionTypeUploadEnd,   &ClientHandler::handleActionUploadEnd},   // 结束上传
    };

    // 查找这个类型对应的函数
    const auto it = kActionHandlers.constFind(type);

    // 没找到 → 打警告，忽略
    if (it == kActionHandlers.constEnd()) {
        qWarning() << "【连接处理】未知 Action 类型:" << type << "账号" << m_accountId;
        return;
    }

    // 找到 → 调用对应的成员函数，执行指令！
    ActionHandler handler = it.value();
    (this->*handler)(action);
}

// 尝试发送 action 中预编码的 payload，成功返回 true。
bool ClientHandler::trySendPreEncodedPayload(const QJsonObject &action)
{
    const QString preEncodedB64 = action.value("pre_encoded_payload_b64").toString();
    if (preEncodedB64.isEmpty()) return false;
    const QByteArray payloadBytes = QByteArray::fromBase64(preEncodedB64.toLatin1());
    if (payloadBytes.isEmpty()) return false;
    socketWrite(payloadBytes);
    return true;
}

// 处理 send_self 动作。
void ClientHandler::handleActionSendSelf(const QJsonObject &action)
{
    const QJsonObject payload = action.value("payload").toObject();
    if (payload.isEmpty()) return;
    if (trySendPreEncodedPayload(action)) {
        return;
    }
    socketWrite(ClientHandlerShared::toJsonCompactBytes(payload));
}

// 处理 send_other 动作。
void ClientHandler::handleActionSendOther(const QJsonObject &action)
{
    // 目标账号
    const QString targetAccount = action.value("account").toString();

    // 要转发的消息内容
    const QJsonObject payload = action.value("payload").toObject();

    // 合法性校验
    if (targetAccount.isEmpty() || payload.isEmpty())
        return;

    // 把账号字符串转成数字
    quint64 targetAccountId = 0;
    if (!tryParseAccountId(targetAccount, targetAccountId))
        return;

    // 拿到消息的 tag
    const QString tag = payload.value("tag").toString();

    // 特殊处理：加好友、同意好友申请
    // 这两种消息需要特殊构造，不能直接转发
    if (tag == QStringLiteral("addfriend") || tag == QStringLiteral("requestpass")) {
        // 转发给目标用户
        const QString senderAccount = payload.value("account").toString();
        if (!senderAccount.isEmpty()) {
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (db.isValid() && db.isOpen()) {
                QSqlQuery qry(db);
                qry.setForwardOnly(true);
                qry.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
                qry.bindValue(":account", senderAccount);
                if (qry.exec() && qry.next()) {
                    QJsonObject fwd;
                    fwd["tag"] = tag == QStringLiteral("addfriend") ? "newaddrequest" : "addrequestpass";
                    fwd["account_number"] = qry.value("account_number").toString();
                    fwd["nickname"] = qry.value("nickname").toString();
                    fwd["gender"] = qry.value("gender").toString();
                    fwd["signature"] = qry.value("signature").toString();
                    QString av = qry.value("avator").toString();
                    fwd["avator"] = av;
                    fwd["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(m_savePath, av);
                    forwardJsonToClient(targetAccountId, fwd);
                    return;
                }
            }
        }
        forwardJsonToClient(targetAccountId, payload);
        return;
    }

    // 优先用预编码好的字节数据转发
    const QString preEncodedB64 = action.value("pre_encoded_payload_b64").toString();

    // 如果有预编码数据
    if (!tag.isEmpty() && !preEncodedB64.isEmpty()) {
        // 把base64字符串转回原始字节
        const QByteArray payloadBytes = QByteArray::fromBase64(preEncodedB64.toLatin1());

        if (!payloadBytes.isEmpty()) {
            // 直接发送已编码好的数据
            queueForwardToClientEncoded(targetAccountId, tag, payloadBytes);
            return;
        }
    }

    // 直接转发JSON消息
    forwardJsonToClient(targetAccountId, payload);
}

// 处理 close 动作。
void ClientHandler::handleActionClose(const QJsonObject &)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
}

// 处理 ack 动作。
void ClientHandler::handleActionAck(const QJsonObject &action)
{
    const QString uuid = action.value("uuid").toString();
    if (uuid.isEmpty()) return;
    if (trySendPreEncodedPayload(action)) {
        return;
    }
    QJsonObject ack;
    ack["tag"] = action.value("tag").toString("messagehavedone");
    ack["uuid"] = uuid;
    socketWrite(ClientHandlerShared::toJsonCompactBytes(ack));
}

// 处理 bind_account 动作。
void ClientHandler::handleActionBindAccount(const QJsonObject &action)
{
    const QJsonObject payload = action.value("payload").toObject();
    const QString account = payload.value("account").toString();
    quint64 accountId = 0;
    if (!tryParseAccountId(account, accountId)) return;
    bindAccountAndKickOld(accountId);
}

// 处理 upload_begin 动作。
void ClientHandler::handleActionUploadBegin(const QJsonObject &action)
{
    dealUploadBegin(action.value("payload").toObject());
}

// 处理 upload_chunk 动作。
void ClientHandler::handleActionUploadChunk(const QJsonObject &action)
{
    dealUploadChunk(action.value("payload").toObject());
}

// 处理 upload_end 动作。
void ClientHandler::handleActionUploadEnd(const QJsonObject &action)
{
    dealUploadEnd(action.value("payload").toObject());
}

// 同线程直调，跨线程排队转发。
void ClientHandler::dispatchReceiveMessageTo(const QPointer<ClientHandler> &target,
                                             const QString &tag,
                                             const QJsonObject &json)
{
    if (target.isNull()) return;
    if (QThread::currentThread() == target->thread()) {
        target->receiveMessage(tag, json);
        return;
    }
    QMetaObject::invokeMethod(target, [target, tag, json]() {
        if (target.isNull()) return;
        target->receiveMessage(tag, json);
    }, Qt::QueuedConnection);
}

// 绑定账号并通知旧连接下线。
void ClientHandler::bindAccountAndKickOld(quint64 accountId)
{
    if (accountId == 0) return;

    QJsonObject qjson;
    qjson["tag"] = "youarekickedoffline";
    QPointer<ClientHandler> old = getClient(accountId);
    if (!old.isNull()) {
        const QString tag = qjson.value("tag").toString();
        dispatchReceiveMessageTo(old, tag, qjson);
    }

    m_accountId = accountId;
    m_aiAnalyzeInFlight = false;
    // 每连接会话令牌，随 loginmessage2 下发；chat_ai_analyze 须携带一致值。
    m_sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    addClient(m_accountId, this);
    qInfo().noquote() << QString("【用户登录】account=%1 在线=%2 线程池(total=%3 working=%4 idle=%5) 当前线程=%6")
        .arg(QString::number(m_accountId)).arg(m_core ? m_core->isValid() : false)
        .arg(ThreadPool::getInstance().totalThreads()).arg(ThreadPool::getInstance().workingThreads()).arg(ThreadPool::getInstance().idleThreads())
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16);
}

// 向目标账号转发 JSON（进入批处理队列）。
void ClientHandler::forwardJsonToClient(quint64 targetAccountId, const QJsonObject &json)
{
    const QString tag = json.value("tag").toString();
    queueForwardToClient(targetAccountId, tag, json);
}

// 转发预编码 JSON（紧凑格式）入批处理队列。
void ClientHandler::queueForwardToClientEncoded(quint64 targetAccountId, const QString &tag, const QByteArray &encodedPayload)
{
    if (targetAccountId == 0 || tag.isEmpty() || encodedPayload.isEmpty()) return;
    bool needScheduleFlush = false;
    {
        QMutexLocker locker(&m_forwardBatchMutex);
        ForwardBatchItem item;
        item.tag = tag;
        item.encodedPayload = encodedPayload;
        m_forwardBatchByTarget[targetAccountId].append(std::move(item));
        if (!m_forwardBatchScheduled.contains(targetAccountId)) {
            m_forwardBatchScheduled.insert(targetAccountId);
        }
        if (!m_forwardFlushScheduled) {
            m_forwardFlushScheduled = true;
            needScheduleFlush = true;
        }
    }
    if (!needScheduleFlush) return;

    QPointer<ClientHandler> thisPtr(this);
    QTimer::singleShot(0, this, [thisPtr]() {
        if (thisPtr.isNull() || thisPtr->isShuttingDownConnection()) return;
        thisPtr->flushForwardBatches();
    });
}

// 转发消息入批处理队列并按需调度刷出。
void ClientHandler::queueForwardToClient(quint64 targetAccountId, const QString &tag, const QJsonObject &json)
{
    // 目标账号无效，直接返回
    if (targetAccountId == 0)
        return;

    bool needScheduleFlush = false;

    {
        QMutexLocker locker(&m_forwardBatchMutex);

        // 创建一条转发项
        ForwardBatchItem item;
        item.tag = tag;

        // 判断：是否需要在接收端查数据库（加好友/同意申请）
        const bool needsDb = (tag == "addfriend" || tag == "requestpass");

        // 不需要DB的普通消息 直接存预编码好的二进制数据
        // 避免后续重复 JSON 序列化，极大提升性能
        if (!needsDb) {
            item.encodedPayload = ClientHandlerShared::toJsonCompactBytes(json);
        }
        // 需要DB的特殊消息：只存最小JSON结构（减少内存占用）
        else {
            item.json["account"] = json.value("account").toString();
        }

        // 把消息按目标账号分组，加入批量队列
        m_forwardBatchByTarget[targetAccountId].append(std::move(item));

        // 该目标账号有待发送数据
        if (!m_forwardBatchScheduled.contains(targetAccountId)) {
            m_forwardBatchScheduled.insert(targetAccountId);
        }

        // 如果还没有调度过“批量发送”，标记需要调度
        if (!m_forwardFlushScheduled) {
            m_forwardFlushScheduled = true;
            needScheduleFlush = true;
        }
    }

    // 如果不需要调度，直接返回
    if (!needScheduleFlush)
        return;

    // 延迟0ms → 放到下一次事件循环执行
    // 等当前这一“批次”的所有消息都入队后，再一次性发送
    QPointer<ClientHandler> thisPtr(this);
    QTimer::singleShot(0, this, [thisPtr]() {
        if (thisPtr.isNull() || thisPtr->isShuttingDownConnection())
            return;

        // 把所有批量消息一次性写入 socket
        thisPtr->flushForwardBatches();
    });
}

// 刷新并投递所有已调度的转发批次（每连接仅安排一次 0ms flush）。
void ClientHandler::flushForwardBatches()
{
    if (isShuttingDownConnection())
        return;

    // 用来存放所有需要发送消息的目标账号
    QList<quint64> targets;

    {
        // 加锁（只用来快速读取，不阻塞业务）
        QMutexLocker locker(&m_forwardBatchMutex);

        // 重置批量发送标记
        m_forwardFlushScheduled = false;

        // 没有要发的 → 直接退出
        if (m_forwardBatchScheduled.isEmpty())
            return;

        // 把所有需要发消息的账号 复制出来
        targets = QList<quint64>(m_forwardBatchScheduled.begin(), m_forwardBatchScheduled.end());

        // 清空待发送标记
        m_forwardBatchScheduled.clear();
    }

    // 遍历每个目标账号，把攒的消息全部发掉
    for (quint64 accountId : targets) {
        // 真正发送这个账号的所有批量消息
        flushForwardBatch(accountId);
    }
}

// 刷新并投递指定账号的转发批次。
void ClientHandler::flushForwardBatch(quint64 targetAccountId)
{
    // 从批量队列里，把发给这个用户的所有消息取出来
    QVector<ForwardBatchItem> batch;
    {
        QMutexLocker locker(&m_forwardBatchMutex);
        auto it = m_forwardBatchByTarget.find(targetAccountId);
        if (it == m_forwardBatchByTarget.end()) {
            return;
        }
        batch = std::move(it.value());
        m_forwardBatchByTarget.erase(it);
    }

    // 没消息就返回
    if (batch.isEmpty()) return;

    // 从对象池里拿一个“发送切片状态”
    // 不每次new/delete，避免内存碎片、极快
    ForwardSliceState *state = nullptr;
    if (!m_forwardSliceStatePool.isEmpty()) {
        state = m_forwardSliceStatePool.takeLast(); // 复用
    } else {
        state = new ForwardSliceState(this); // 新建
        state->timer.setParent(state);
        state->timer.setSingleShot(true);
        state->timer.setInterval(0);
    }

    // 把目标账号 + 消息列表放进状态对象
    state->reset(targetAccountId, std::move(batch));
    m_activeForwardSlices.append(state);

    QPointer<ClientHandler> thisPtr(this);

    // 定时器 0ms 延迟：下一次事件循环再发
    QObject::connect(&state->timer, &QTimer::timeout, &state->timer, [thisPtr, state]() {
        if (thisPtr.isNull()) {
            if (state) state->deleteLater();
            return;
        }
        if (thisPtr->isShuttingDownConnection()) {
            thisPtr->recycleForwardSliceState(state); // 回收
            return;
        }

        // 找到目标客户端
        QPointer<ClientHandler> targetClient = thisPtr->getClient(state->targetAccountId);
        if (targetClient.isNull()) {
            thisPtr->recycleForwardSliceState(state); // 不在线 → 回收
            return;
        }

        // ==================== 切片发送 每次只发一小段 ====================
        const int total = state->batch.size();
        const int start = state->index;
        if (start >= total) {
            thisPtr->recycleForwardSliceState(state); // 发完了 → 回收
            return;
        }

        // 每次发 N 条
        const int end = qMin(total, start + ServerConfigDefaults::defaultForwardBatchItemsPerSlice());

        // 发送这一小片
        for (int i = start; i < end; ++i) {
            const ForwardBatchItem &item = state->batch.at(i);

            // 有预编码数据 → 直接发
            if (!item.encodedPayload.isEmpty()) {
                const QByteArray &payloadBytes = item.encodedPayload;

                // 如果在目标线程 → 直接发
                if (QThread::currentThread() == targetClient->thread()) {
                    targetClient->sendJsonToSocketQueued(payloadBytes);
                }
                // 不在 → 安全投递到目标线程
                else {
                    QMetaObject::invokeMethod(targetClient, [targetClient, payloadBytes]() {
                        if (targetClient.isNull()) return;
                        targetClient->sendJsonToSocketQueued(payloadBytes);
                    }, Qt::QueuedConnection);
                }
            }
            // 无预编码 → 走普通接收逻辑
            else {
                thisPtr->dispatchReceiveMessageTo(targetClient, item.tag, item.json);
            }
        }

        // 移动下标：下一次发下一片
        state->index = end;

        // 没发完 → 继续触发下一次切片发送
        if (state->index < total) {
            state->timer.start();
        }
        // 发完了 → 回收对象
        else {
            thisPtr->recycleForwardSliceState(state);
        }
    });

    // 启动第一次切片发送
    state->timer.start();
}

// ===== 生命周期与 socket 事件 =====
// 开始处理连接并挂接 socket 信号。
void ClientHandler::startHandling()
{
    if (!m_socket) return;
    qInfo() << "开始处理连接:" << m_socket->peerAddress().toString() << ":" << m_socket->peerPort();
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &ClientHandler::onBytesWritten);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientHandler::onDisconnected);
}

// 处理可读事件。
void ClientHandler::onReadyRead()
{

    // 如果连接正在关闭，直接退出，不再处理任何数据
    if (isShuttingDownConnection())
        return;

    // 如果套接字无效 或 未连接 → 退出
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        static std::atomic<quint64> sSocketInvalidNoisyCounter{0};
        if (shouldLogNoisy(sSocketInvalidNoisyCounter)) {
            qWarning() << "【连接处理】套接字无效或未连接";
        }
        return;
    }


    // 刷新超时计时器：收到数据 = 连接活跃，不超时断开
    resetTimeoutTimer();

    // 启动分片计时：限制单次读耗时，避免阻塞事件循环
    QElapsedTimer sliceTimer;
    sliceTimer.start();

    // 本次循环处理的数据包数量统计
    int processedPackets = 0;

    while (m_socket->bytesAvailable() > 0)
    {
        bool processed = false;

        // 根据当前接收状态，执行不同的读取逻辑
        switch (m_recvState) {
        case RecvState::WaitHeader:
            processed = receiveHeader();  // 等待并读取【协议头】
            break;
        case RecvState::WaitData:
            processed = receiveData();    // 等待并读取【包体数据】
            break;
        }

        // 如果本次没处理成功（数据不够/出错），退出循环
        if (!processed)
            break;

        ++processedPackets;

        // 分片读取，防止事件循环阻塞
        // 处理包数达到上限或本次读取耗时达到时间上限就暂停读取
        if (processedPackets >= ServerConfigDefaults::defaultReadyReadMaxPacketsPerSlice() ||
            sliceTimer.elapsed() >= ServerConfigDefaults::defaultReadyReadMaxMsPerSlice())
        {
            // 如果还有剩余数据没读完，调度下一轮继续读
            if ( m_socket && m_socket->bytesAvailable() > 0) {
                // 弱指针保护，防止对象销毁后崩溃
                QPointer<ClientHandler> thisPtr(this);

                // 用队列调用，把剩余读取放到下一次事件循环
                QMetaObject::invokeMethod(this, [thisPtr]() {
                    if (!thisPtr.isNull() && !thisPtr->isShuttingDownConnection())
                        {
                        thisPtr->onReadyRead();
                    }
                }, Qt::QueuedConnection);
            }

            break;
        }
    }

    // 上报性能指标：本次读取分片耗时
    ClientHandlerShared::emitPerfMetric("onReadyRead_slice_ms", sliceTimer.elapsed());
}

// 处理断开连接。
void ClientHandler::onDisconnected()
{
    m_connectionClosed = true;
    // 断线后 AI 状态与令牌作废，避免异步回包误用旧连接。
    m_aiAnalyzeInFlight = false;
    m_sessionToken.clear();
    if (m_socket) {
        (void)m_socket->readAll();
        QObject::disconnect(m_socket, nullptr, this, nullptr);
        qInfo() << "断开连接 账号:" << m_accountId << "对端:" << m_socket->peerAddress().toString() << ":" << m_socket->peerPort();
    }
    if (m_core) {
        m_core->abandonConnectionResources(this);
    }
    // 断开时立刻释放上传会话内存；删文件排到下一事件循环，让同连接上的异步写盘先收尾，避免与线程池写文件竞态。
    {
        QList<QString> partialFiles;
        partialFiles.reserve(m_uploads.size());
        for (auto it = m_uploads.constBegin(); it != m_uploads.constEnd(); ++it) {
            const QString p = it.value().filePath;
            if (!p.isEmpty())
                partialFiles.append(p);
        }
        m_uploads.clear();
        updateUploadCleanupRegistration();
        const QList<QString> pathsCopy = partialFiles;
        QTimer::singleShot(0, QCoreApplication::instance(), [pathsCopy]() {
            for (const QString &path : pathsCopy) {
                if (!path.isEmpty())
                    QFile::remove(path);
            }
        });
    }
    abortActiveForwardSlices();
    purgeQueuesAndSessions();
    this->deleteLater();
}

// 套接字有数据写出后继续泵送。
void ClientHandler::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    pumpSocketWrite();
}

// 处理跨连接转发消息。
void ClientHandler::receiveMessage(const QString &tag, const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    // fast-path：这几类转发仅需原样写回 socket，不需要线程池业务处理。
    if (tag == "youaredeleted" ||
        tag == "youarekickedoffline" ||
        tag == "yourmessages" ||
        tag == "changeinfor") {
        sendJsonToSocketQueued(json);
        return;
    }

    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,
        [thisPtr, tag, json]() {
            if (thisPtr.isNull() || thisPtr->isShuttingDownConnection()) return;
            using ForwardHandler = void (ClientHandler::*)(const QJsonObject &);
            static const QHash<QString, ForwardHandler> kForwardHandlers = {
                {"addfriend", &ClientHandler::forwordAddFriendRequest},
                {"requestpass", &ClientHandler::forwordRequestPass},
                {"youaredeleted", &ClientHandler::forwordYouAreDeleted},
                {"youarekickedoffline", &ClientHandler::forwordKickedOffline},
                {"yourmessages", &ClientHandler::forwordMessages},
                {"changeinfor", &ClientHandler::forwordChangeInfor},
            };

            const auto it = kForwardHandlers.constFind(tag);
            if (it == kForwardHandlers.constEnd()) {
                static std::atomic<quint64> sUnknownForwardTagNoisyCounter{0};
                if (shouldLogNoisy(sUnknownForwardTagNoisyCounter)) {
                    qWarning() << "未知转发tag:" << tag;
                }
                return;
            }
            (thisPtr.data()->*(it.value()))(json);
        });
}

// ===== 发送 =====
// 将 JSON 回投到连接线程并发送。
void ClientHandler::sendJsonToSocketQueued(const QJsonObject &json)
{
    sendJsonToSocketQueued(ClientHandlerShared::toJsonCompactBytes(json));
}

// 将已编码 JSON 回投到连接线程并发送。
void ClientHandler::sendJsonToSocketQueued(const QByteArray &messageData)
{
    QPointer<ClientHandler> thisPtr(this);
    if (thisPtr.isNull() || messageData.isEmpty()) return;
    ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, messageData]() {
        if (thisPtr.isNull() || thisPtr->isShuttingDownConnection()) return;
        thisPtr->socketWrite(messageData);
    });
}

