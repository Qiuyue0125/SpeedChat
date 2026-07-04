#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

/**
 * @file ClientHandler.h
 * 单客户端连接：协议解析、限流、任务投递与响应发送。
 */

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <exception>
#include <functional>
#include "SocketDepend.h"
#include <QtGlobal>

class ServerCore;
class QTcpSocket;

// 客户端连接处理器：协议收发、业务调度、转发与上传下载。
class ClientHandler : public QObject
{
    Q_OBJECT
public:
    using WorkerActionList = QJsonArray;

    // 构造连接处理器
    explicit ClientHandler(QTcpSocket *socket, ServerCore *core, int idleTimeoutMs = 60000, QObject *parent = nullptr);
    // 析构处理器并释放连接相关资源
    ~ClientHandler();
    // 套接字是否仍处于已连接（供 ServerCore 文档泵注册等判断）。
    bool isTcpConnected() const;
    // 连接已进入断开清理（异步任务应停止回投业务逻辑）。
    bool isShuttingDownConnection() const { return m_connectionClosed; }

private:
    // 将账号字符串解析为 accountId（纯数字）。失败返回 false。
    static bool tryParseAccountId(const QString &account, quint64 &outAccountId);

    class ForwardSliceState;

    // 回收切片状态对象（对象池复用，减少频繁 new/delete）。
    void recycleForwardSliceState(ForwardSliceState *state);

    // ===== 发送队列与限速 =====
    // 文档下载分片发送与限速。
    bool enqueueDocPacket(const QJsonObject &obj);
    // 发送已编码 JSON 数据包（避免重复 toJson）。
    bool enqueueDocPacketEncoded(const QByteArray &payload);
    // 发送二进制数据入队。
    bool enqueueBinaryPacket(const QByteArray &body);
    // 文档下载流式发送：按队列/Socket 余量从磁盘读分片，避免大文件占满内存或丢包。
    bool docPipelineHasRoomFor(int extraBytes) const;
    void resetDocDownloadSession();
    bool beginStreamingDocDownload(const QJsonObject &ready);
    void pumpDocDownloadSource(QElapsedTimer &sliceTimer);
    static QByteArray buildDocumentChunkBody(const QByteArray &uuidBin, qint64 seq, const QByteArray &chunk);

public slots:
    // 发送队列数据（由 ServerTimers 统一调用）。
    void pumpDocSend();

private:
    // ===== 在线客户端管理 =====
    // 添加在线客户端。
    void addClient(quint64 accountId, ClientHandler *client);
    // 移除在线客户端。
    void removeClient(quint64 accountId);
    // 获取在线客户端。
    ClientHandler* getClient(quint64 accountId);
    // 重置连接空闲超时计时器。
    void resetTimeoutTimer();

public slots:
    // 由 ServerTimers 调用：检查空闲超时并断开。
    void checkIdleTimeout();
    // 由 ServerTimers 调用：清理超时上传会话。
    void checkUploadCleanup();

private:
    // 根据 m_uploads 状态更新 upload cleanup 注册。
    void updateUploadCleanupRegistration();

    // ===== socket 原始发送 =====
    // 发送数据到套接字。
    void socketWrite(const QByteArray& data);
    // 发送低优先级数据到套接字（队列满可丢弃）。
    void socketWriteLowPriority(const QByteArray& data);
    // 头体分离入队（背压）。
    bool enqueueSocketPacket(const QByteArray &body, bool droppable, uint8_t payloadType = PAYLOAD_JSON);
    // 尝试发送原始队列。
    void pumpSocketWrite();

    // ===== 协议接收与解析调度 =====
    // 接收协议头。
    bool receiveHeader();
    // 接收协议体。
    bool receiveData();
    // 重置接收状态机。
    void resetRecvState();
    // 处理上传分片二进制数据。
    void handleUploadChunkBytes(const QString &uuid, qint64 seq, const QByteArray &bin);
    // 调度上传分片异步写盘。
    void dispatchUploadChunkWrite(const QString &uuid);
    // 断开/析构：停止转发切片定时器并清空其 payload，避免 QByteArray 长期挂住。
    void abortActiveForwardSlices();
    // 释放队列与上传会话内存（不含 socket；上传临时文件删除由 onDisconnected 另行调度）。
    void purgeQueuesAndSessions();
    // JSON 包入队并异步解析。
    bool enqueueJsonParsePacket(const QByteArray &packet);
    // 刷新限流窗口计数（按 Settings.ini 的 RateLimitWindowMs）。
    void updateRateLimitWindow(qint64 nowMs);
    // 启动下一条 JSON 解析任务。
    void dispatchNextJsonParse();
    // 二进制包入队并异步解析。
    bool enqueueBinaryParsePacket(const QByteArray &packet);
    // 启动下一条二进制解析任务。
    void dispatchNextBinaryParse();
    // 按 tag 分发已解析 JSON。
    void processParsedJsonObject(const QString &tag, const QJsonObject &jsonObj);
    // 消费线程任务返回的 Action 列表。
    void consumeWorkerActions(const WorkerActionList &actions);
    // 执行单个 Action。
    void executeWorkerAction(const QJsonObject &action);

    // ===== Action 处理 =====
    // 从 action 中提取预编码 payload 并直接发送，成功返回 true。
    bool trySendPreEncodedPayload(const QJsonObject &action);
    // 处理 send_self 动作。
    void handleActionSendSelf(const QJsonObject &action);
    // 处理 send_other 动作。
    void handleActionSendOther(const QJsonObject &action);
    // 处理 close 动作。
    void handleActionClose(const QJsonObject &action);
    // 处理 ack 动作。
    void handleActionAck(const QJsonObject &action);
    // 处理 bind_account 动作。
    void handleActionBindAccount(const QJsonObject &action);
    // 处理 upload_begin 动作。
    void handleActionUploadBegin(const QJsonObject &action);
    // 处理 upload_chunk 动作。
    void handleActionUploadChunk(const QJsonObject &action);
    // 处理 upload_end 动作。
    void handleActionUploadEnd(const QJsonObject &action);

    // ===== 转发与账号绑定 =====
    // 同线程直调，跨线程排队转发。
    void dispatchReceiveMessageTo(const QPointer<ClientHandler> &target,
                                  const QString &tag,
                                  const QJsonObject &json);
    // 绑定账号并通知旧连接下线。
    void bindAccountAndKickOld(quint64 accountId);
    // 向指定在线账号转发 JSON。
    void forwardJsonToClient(quint64 targetAccountId, const QJsonObject &json);
    // 转发预编码 JSON（紧凑格式）入批处理队列。
    void queueForwardToClientEncoded(quint64 targetAccountId, const QString &tag, const QByteArray &encodedPayload);
    // 转发消息入批处理队列。
    void queueForwardToClient(quint64 targetAccountId, const QString &tag, const QJsonObject &json);
    // 刷新并投递所有已调度的转发批次（每连接仅安排一次 0ms flush）。
    void flushForwardBatches();
    // 刷新并投递指定账号的转发批次。
    void flushForwardBatch(quint64 targetAccountId);

    // ===== 上传处理 =====
    // 处理上传开始（路由入口）。
    void dealUploadBegin(const QJsonObject &json);
    // 处理上传分片（路由入口）。
    void dealUploadChunk(const QJsonObject &json);
    // 处理上传结束（路由入口）。
    void dealUploadEnd(const QJsonObject &json);
    // 结束上传会话（成功/失败）。
    void finishUploadSession(const QString &uuid, bool success, const QString &message = QString());

public slots:
    // 开始处理连接并挂接 socket 信号。
    void startHandling();
    // 处理可读事件。
    void onReadyRead();
    // 处理断开连接。
    void onDisconnected();
    // 套接字有数据写出后继续泵送。
    void onBytesWritten(qint64 bytes);
    // 处理跨连接转发消息。
    void receiveMessage(const QString &tag, const QJsonObject &json);

private:
    // ===== 业务路由处理 =====
    // 处理登录请求。
    void dealLogin(const QJsonObject &json);
    // 处理注册请求。
    void dealRegister(const QJsonObject &json);
    // 处理找回密码步骤一。
    void dealFindpassword1(const QJsonObject &json);
    // 处理找回密码步骤二。
    void dealFindpassword2(const QJsonObject &json);
    // 处理找回密码步骤三。
    void dealFindpassword3(const QJsonObject &json);
    // 处理登录初始化步骤零。
    void dealLoginMes0(const QJsonObject &json);
    // 处理登录初始化步骤一。
    void dealLoginMes1(const QJsonObject &json);
    // 处理登录初始化步骤二。
    void dealLoginMes2(const QJsonObject &json);
    // 处理删除好友。
    void dealDeleteFriend(const QJsonObject &json);
    // 处理搜索账号。
    void dealSearchAccount(const QJsonObject &json);
    // 处理更新资料。
    void dealChangeInformation(const QJsonObject &json);
    // 处理修改密码。
    void dealChangePassword(const QJsonObject &json);
    // 处理修改密码二。
    void dealChangePassword2(const QJsonObject &json);
    // 处理注销。
    void dealLogout(const QJsonObject &json);
    // 处理添加好友。
    void dealAddFriends(const QJsonObject &json);
    // 处理添加好友回应。
    void dealAddFriendsRespond(const QJsonObject &json);
    // 处理聊天消息。
    void dealMessages(const QJsonObject& json);
    // 处理下一条文档消息。
    Q_INVOKABLE void dealNextMessage();
    // 处理下载文件请求。
    void dealAskDocument(const QJsonObject &json);
    // 处理网盘文件下载请求（任意登录用户凭 file_id 下载）。
    void dealAskCloudFile(const QJsonObject &json);
    // DB 任务返回后启动流式下载或下发 document_error。
    void handleDocDownloadDbResult(const QByteArray &result, const QString &uuidStr);
    // 查询网盘文件信息。
    void dealSearchCloudFile(const QJsonObject &json);
    // 列出当前用户上传的网盘文件。
    void dealListMyCloudFiles(const QJsonObject &json);
    // 处理索要好友信息。
    void dealAskForFriend(const QJsonObject &json);
    // 处理消息已读。
    void dealMessageRead(const QJsonObject &json);
    // 处理登录消息已读。
    void dealLoginMessageRead(const QJsonObject &json);
    // 聊天 AI 分析（校验 session_token、并发与冷却；线程池内查库并调 LLM）。
    void dealChatAiAnalyze(const QJsonObject &json);

private:
    // ===== 转发消息封装 =====
    // 转发好友申请。
    void forwordAddFriendRequest(const QJsonObject &json);
    // 转发申请通过。
    void forwordRequestPass(const QJsonObject &json);
    // 转发被删除通知。
    void forwordYouAreDeleted(const QJsonObject &json);
    // 转发挤下线通知。
    void forwordKickedOffline(const QJsonObject &json);
    // 转发聊天消息。
    void forwordMessages(const QJsonObject &json);
    // 转发资料更新。
    void forwordChangeInfor(const QJsonObject &json);
    // 将 JSON 回投到连接线程并发送。
    void sendJsonToSocketQueued(const QJsonObject &json);
    // 将已编码 JSON 回投到连接线程并发送。
    void sendJsonToSocketQueued(const QByteArray &messageData);
    // AI 分析结束：清除进行中标记并回包（须在连接所属线程调用）。
    void postAiAnalyzeResult(const QJsonObject &out);

private:
    struct PendingMessageItem {
        QJsonObject json;
        int retryCount = 0;
    };

    // 接收状态
    RecvState m_recvState = RecvState::WaitHeader;
    // 待接收数据长度
    uint32_t m_expectedDataLen = 0;
    uint8_t m_expectedPayloadType = PAYLOAD_JSON;
    // 空闲超时（由 ServerTimers 统一检查）
    int m_idleTimeoutMs = 60000;
    qint64 m_lastActivityMs = 0;
    QTcpSocket *m_socket;
    // 缓存目录
    QString m_savePath;
    // 接收缓存
    QByteArray m_recvBuffer;
    QPointer<ServerCore> m_core;
    // 当前账号
    quint64 m_accountId = 0;
    // 本连接登录后下发的会话令牌（AI 等敏感接口需携带，防伪造账号字段刷接口）
    QString m_sessionToken;
    // 本连接是否有一次 AI 分析任务仍在执行（防并发耗尽 token）
    bool m_aiAnalyzeInFlight = false;

    // 限流窗口（用于 heart / unknown tag）
    qint64 m_rateLimitWindowStartMs = 0;
    int m_heartCountInWindow = 0;
    int m_unknownTagCountInWindow = 0;
    // 队列锁
    QMutex m_queMutex;
    // 待处理消息队列
    QQueue<PendingMessageItem> m_messageQueue;
    // 是否正在处理队列
    bool m_isSending = false;
    int m_messageMaxRetries = 3;

    QTimer *m_docSendTimer = nullptr;
    struct OutPacket {
        uint8_t payloadType = PAYLOAD_JSON;
        QByteArray body;
    };
    QQueue<OutPacket> m_docSendQueue;
    qint64 m_docTokens = 0;
    qint64 m_docLastRefillMs = 0;
    qint64 m_docSendQueueBytes = 0;

    struct RawOutPacket {
        QByteArray header;
        QByteArray body;
        qint64 headerSent = 0;
        qint64 bodySent = 0;
        bool droppable = false;
    };
    QQueue<RawOutPacket> m_outQueue;
    qint64 m_outQueueBytes = 0;
    struct ForwardBatchItem {
        QString tag;
        // 对于可直接发送的转发消息，优先存预编码 bytes，避免重复 JSON 编码与对象拷贝。
        QByteArray encodedPayload;
        // 需要业务处理（如查询 DB）的转发消息保留最小 JSON（通常仅包含必要字段）。
        QJsonObject json;
    };
    class ForwardSliceState final : public QObject
    {
    public:
        explicit ForwardSliceState(QObject *parent = nullptr) : QObject(parent) {}

        // 绑定转发目标账号与批次，并将当前发送游标置零。
        void reset(quint64 accountId, QVector<ForwardBatchItem> &&items)
        {
            targetAccountId = accountId;
            batch = std::move(items);
            index = 0;
        }

        quint64 targetAccountId = 0;
        QVector<ForwardBatchItem> batch;
        int index = 0;
        QTimer timer;
    };
    QMutex m_forwardBatchMutex;
    QHash<quint64, QVector<ForwardBatchItem>> m_forwardBatchByTarget;
    QSet<quint64> m_forwardBatchScheduled;
    bool m_forwardFlushScheduled = false;
    QVector<ForwardSliceState*> m_forwardSliceStatePool;
    int m_forwardSliceStatePoolMax = 64;
    QVector<ForwardSliceState*> m_activeForwardSlices;

    struct UploadSession {
        bool active = false;
        QString uuid;
        QString sender;
        QString receiver;
        QString filename;
        QString messageType = "document";
        QString timestamp;
        qint64 totalBytes = 0;
        qint64 receivedBytes = 0;
        int chunkBytes = 0;  // 创建时由 serverUploadChunkBytes() 初始化
        qint64 nextSeq = 0;
        QString filePath;
        QString fileUrl;
        qint64 lastActivityMs = 0;
        struct PendingChunk {
            qint64 seq = 0;
            QByteArray data;
        };
        QQueue<PendingChunk> pendingChunks;
        int pendingBytes = 0;
        bool writeInFlight = false;
        bool endRequested = false;
        bool isCloud = false;
    };
    // 上传会话集合（由 ServerTimers 统一调用 checkUploadCleanup）
    QHash<QString, UploadSession> m_uploads;
    // 文档/网盘下载流式发送会话（按 pumpDocSend 节奏读盘入队）
    struct DocDownloadSession {
        bool active = false;
        QString uuid;
        QFile file;
        QByteArray uuidBin;
        int chunkBytes = 0;
        qint64 totalBytes = 0;
        qint64 totalChunks = 0;
        qint64 nextSeq = 0;
        bool endEnqueued = false;
    };
    DocDownloadSession m_docDownload;
    // 已断开：忽略异步解析/写盘回投，避免在清空队列后继续堆积。
    bool m_connectionClosed = false;
    QQueue<QByteArray> m_jsonParseQueue;
    qint64 m_jsonParseQueueBytes = 0;
    bool m_jsonParseInFlight = false;
    QQueue<QByteArray> m_binaryParseQueue;
    qint64 m_binaryParseQueueBytes = 0;
    bool m_binaryParseInFlight = false;
};

// 数据库任务执行器
class DbLambdaWorker : public QObject
{
    Q_OBJECT
public:
    explicit DbLambdaWorker(std::function<QByteArray()> task, QObject *parent = nullptr)
        : QObject(parent), m_task(std::move(task)) {}

public slots:
    // 在工作线程执行封装的任务，完成后发出 resultReady（含耗时毫秒）。
    void process() {
        QElapsedTimer timer;
        timer.start();
        QByteArray result;
        try {
            result = m_task ? m_task() : QByteArray();
        } catch (const std::exception &e) {
            qWarning() << "【线程池】任务异常，将释放线程:" << e.what();
        } catch (...) {
            qWarning() << "【线程池】任务未知异常，将释放线程";
        }
        emit resultReady(result, timer.elapsed());
    }

signals:
    void resultReady(const QByteArray &result, qint64 dbMs);

private:
    std::function<QByteArray()> m_task;
};

#endif
