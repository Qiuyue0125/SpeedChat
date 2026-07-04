#ifndef CLIENTHANDLERSHARED_H
#define CLIENTHANDLERSHARED_H

/**
 * @file ClientHandlerShared.h
 * ClientHandler 相关业务处理（共享实现单元）。
 */

#include "ClientHandler.h"
#include "ServerConfigDefaults.h"
#include "SocketDepend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QSharedPointer>
#include <atomic>
#include <functional>
#include <type_traits>

// ClientHandler 共享工具与调度逻辑：JSON/二进制解析、Action 构造、线程调度、加密、头像缓存等。
namespace ClientHandlerShared {

// ===== TaskRetryQueue：统一重试队列，单一定时器驱动，避免高负载下大量 QTimer::singleShot =====
class TaskRetryQueue
{
public:
    // 获取单例实例。
    static TaskRetryQueue &instance()
    {
        static TaskRetryQueue inst;
        return inst;
    }
    // 将任务加入重试队列，delayMs 毫秒后执行。
    void schedule(std::function<void()> fn, int delayMs);

private:
    // 私有构造，启动定时器轮询队列。
    TaskRetryQueue();
    // 定时器回调，检查并执行到期的重试任务。
    void onTick();
    QTimer *m_timer = nullptr;
    QMutex m_mutex;
    QQueue<QPair<std::function<void()>, qint64>> m_queue;
};

// ===== 工具函数 =====
// 按周期采样，用于限流日志。每 every 次返回 true 一次；every=0 表示使用 defaultShouldSampleEvery。
bool shouldSample(std::atomic<quint64> &counter, quint64 every = 0);
// 输出性能指标到日志，格式：PERF name=value。
void emitPerfMetric(const char *name, qint64 value);
// 将 QJsonObject 转为紧凑 JSON 字节数组。
QByteArray toJsonCompactBytes(const QJsonObject &obj);
// 将二进制 UUID（16 字节）转为字符串。
QString binaryUuidToStringStatic(const QByteArray &binaryUuid);

// ===== 加密与盐值 =====
// 对 plainText+salt 做 SHA256，返回十六进制字符串。
QString sha256Hex(const QString &plainText, const QString &salt);
// 生成 16 字符随机盐值。
QString generateSaltStatic();
// 使用盐值加密明文，返回密文十六进制。
QString encryptWithSaltStatic(const QString &plainText, const QString &salt);
// 校验输入与盐值加密后是否等于密文。
bool verifyWithSaltStatic(const QString &inputText, const QString &salt, const QString &cipherText);

// ===== 头像缓存 =====
// 根据头像 URL 读取文件并返回 Base64 字符串，带缓存。
QString getAvaFromUrlStatic(const QString &savePath, const QString &url);

// ===== 协议解析 =====
// 解析 JSON 包，提取 tag 和 jsonObj。成功返回 true。
bool parseJsonPacket(const QByteArray &payload, QString &tag, QJsonObject &jsonObj);
// 轻量提取 JSON 中的 tag 字段（仅用于背压时快速判定丢弃策略）。
// 成功返回 true，并将 tag 写入 tagOut。
bool tryExtractJsonTagFast(const QByteArray &payload, QString &tagOut);
// 解析二进制分片包，提取 uuid、seq、chunk。失败时 error 非空。
bool parseBinaryChunkPacket(const QByteArray &payload, QString &uuid, qint64 &seq,
                            QByteArray &chunk, QString &error);

// ===== Action 构造 =====
// 构造 send_self 类型 Action。
QJsonObject makeSendSelfAction(const QJsonObject &payload);
// 构造 send_other 类型 Action。
QJsonObject makeSendOtherAction(const QString &account, const QJsonObject &payload);
// 从已编码字节构造 Action，避免同一 payload 重复编码。
QJsonObject makeSendSelfActionFromEncoded(const QByteArray &encoded);
QJsonObject makeSendOtherActionFromEncoded(const QString &account, const QByteArray &encoded);
// 构造 ack 类型 Action。
QJsonObject makeAckAction(const QString &uuid, const QString &tag = QStringLiteral("messagehavedone"));
// 将单个 payload 编码为 send_self Action 列表。
ClientHandler::WorkerActionList encodeSingleSendSelf(const QJsonObject &payload);
// 将单个 payload 编码为 send_other Action 列表。
ClientHandler::WorkerActionList encodeSingleSendOther(const QString &account, const QJsonObject &payload);
// 构造简单应答 Action（tag + answer）。
ClientHandler::WorkerActionList makeAnswerAction(const QString &tag, const QString &answer);
// 将 Action 列表编码为可传输格式（当前实现直接返回）。
ClientHandler::WorkerActionList encodeActions(const ClientHandler::WorkerActionList &actions);
// 从 Action 列表中提取第一个 send_self 的 payload。
QJsonObject extractFirstSendSelfPayload(const ClientHandler::WorkerActionList &actions);

// ===== 线程调度 =====
// 在 obj 所在线程执行 fn。同线程则直接调用，跨线程则 QueuedConnection。
template<typename Fn>
void runOnObjectThread(QObject *obj, Fn &&fn)
{
    if (!obj) return;
    if (QThread::currentThread() == obj->thread()) {
        fn();
        return;
    }
    QMetaObject::invokeMethod(obj, [fn = std::forward<Fn>(fn)]() mutable {
        fn();
    }, Qt::QueuedConnection);
}
// 异步执行 DB 任务（在线程池中），完成后在 context 线程调用 onDone。支持 Task 返回 QByteArray 或 QJsonArray。
template<typename Task, typename OnDone>
void runDbTask(QObject *context, Task &&task, OnDone &&onDone)
{
    if (!context) return;
    using TResult = std::decay_t<decltype(std::declval<Task &>()())>;
    static_assert(!std::is_void<TResult>::value, "runDbTask task must return a value");

    auto taskBytes = [task = std::forward<Task>(task)]() -> QByteArray {
        TResult result = task();
        if constexpr (std::is_same_v<TResult, QByteArray>) {
            return result;
        } else if constexpr (std::is_same_v<TResult, QJsonArray>) {
            return QJsonDocument(result).toJson(QJsonDocument::Compact);
        } else {
            return QByteArray();
        }
    };

    auto onDoneBytes = [onDone = std::forward<OnDone>(onDone)](const QByteArray &bytes) {
        if constexpr (std::is_same_v<TResult, QByteArray>) {
            onDone(bytes);
        } else if constexpr (std::is_same_v<TResult, QJsonArray>) {
            if (bytes.isEmpty()) {
                onDone(QJsonArray());
                return;
            }
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                onDone(doc.array());
            } else {
                onDone(QJsonArray());
            }
        }
    };

    runDbTaskImpl(context, std::move(taskBytes), std::move(onDoneBytes));
}
void runDbTaskImpl(QObject *context, std::function<QByteArray()> task, std::function<void(const QByteArray &)> onDone);
// 将业务任务投递到线程池执行，可选 onFinished 回调。
void dispatchBusinessTaskToThreadPool(QObject *context,
                                      std::function<void()> task,
                                      std::function<void()> onFinished = {});
// 统一重试调度：将任务加入重试队列，由单一定时器驱动。
void scheduleRetry(std::function<void()> fn, int delayMs = -1);
// 在线程池中异步删除文件。
void scheduleFileRemoveOnThreadPool(QObject *context, const QString &path);

}  // namespace ClientHandlerShared

// TaskRetryQueue 实现

// 将任务加入重试队列，delayMs 毫秒后执行。
inline void ClientHandlerShared::TaskRetryQueue::schedule(std::function<void()> fn, int delayMs)
{
    if (!fn) return;
    const qint64 readyAt = QDateTime::currentMSecsSinceEpoch() + delayMs;
    QMutexLocker lock(&m_mutex);
    m_queue.enqueue({std::move(fn), readyAt});
}

// 私有构造，启动定时器轮询队列。
inline ClientHandlerShared::TaskRetryQueue::TaskRetryQueue()
{
    m_timer = new QTimer(QCoreApplication::instance());
    m_timer->setInterval(ServerConfigDefaults::defaultDbDispatchRetryIntervalMs());
    m_timer->setSingleShot(false);
    QObject::connect(m_timer, &QTimer::timeout, m_timer, [this]() { onTick(); });
    m_timer->start();
}

// 定时器回调，检查并执行到期的重试任务。
inline void ClientHandlerShared::TaskRetryQueue::onTick()
{
    QList<std::function<void()>> toRunList;
    {
        QMutexLocker lock(&m_mutex);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int n = m_queue.size();
        if (n <= 0) {
            return;
        }
        // 单次 tick 最多执行的任务数（避免长时间占用事件循环）。
        // 这里取一个保守常量；如需可再配置化。
        const int maxRun = 8;
        for (int i = 0; i < n; ++i) {
            auto p = m_queue.dequeue();
            if (p.second <= now && toRunList.size() < maxRun) {
                toRunList.append(std::move(p.first));
                continue;
            }
            m_queue.enqueue(std::move(p));
        }
    }
    for (auto &fn : toRunList) {
        if (fn) fn();
    }
}

#endif // CLIENTHANDLERSHARED_H
