/**
 * @file ClientHandlerShared.cpp
 * ClientHandler 与数据库线程间共享：Action 编码、投递、限流与工具。
 */
#include "ClientHandlerShared.h"
#include "ClientHandler.h"
#include "Logger.h"
#include "ServerConfigDefaults.h"
#include "ThreadPool.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSharedPointer>
#include <QUuid>
#include <cstring>

// ===== 工具函数 =====

namespace ClientHandlerShared {

// 按周期采样，用于限流日志。每 every 次返回 true 一次；every=0 表示使用 defaultShouldSampleEvery
bool shouldSample(std::atomic<quint64> &counter, quint64 every)
{
    if (every == 0) every = ServerConfigDefaults::defaultShouldSampleEvery();
    const quint64 v = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return every > 0 && (v % every) == 0;
}

// 输出性能指标到日志，格式：PERF name=value
void emitPerfMetric(const char *name, qint64 value)
{
    Logger::logPerfMetric(name, value);
}

// 将 QJsonObject 转为紧凑 JSON 字节数组。
QByteArray toJsonCompactBytes(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// 将二进制 UUID（16 字节）转为字符串。
QString binaryUuidToStringStatic(const QByteArray &binaryUuid)
{
    if (binaryUuid.size() != 16) return QString();
    const QUuid uuid = QUuid::fromRfc4122(binaryUuid);
    if (uuid.isNull()) return QString();
    return uuid.toString();
}

// ===== 加密与盐值 =====

// 对 plainText+salt 做 SHA256，返回十六进制字符串。
QString sha256Hex(const QString &plainText, const QString &salt)
{
    QByteArray bytes = (plainText + salt).toUtf8();
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

// 生成 16 字符随机盐值。
QString generateSaltStatic()
{
    return QUuid::createUuid().toString().replace("-", "").mid(0, 16);
}

// 使用盐值加密明文，返回密文十六进制。
QString encryptWithSaltStatic(const QString &plainText, const QString &salt)
{
    QByteArray bytes = (plainText + salt).toUtf8();
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

// 校验输入与盐值加密后是否等于密文。
bool verifyWithSaltStatic(const QString &inputText, const QString &salt, const QString &cipherText)
{
    return encryptWithSaltStatic(inputText, salt) == cipherText;
}

// ===== 头像缓存 =====

struct AvatarCache {
    QHash<QString, QString> hash;
    /// 访问时间顺序：队首为 LRU（优先淘汰），队尾为 MRU（最近使用）。
    QList<QString> lruOrder;
};
static AvatarCache &getAvatarCache()
{
    static AvatarCache cache;
    return cache;
}
static QMutex s_avatarCacheMutex;

// 将 key 标记为最近使用：从当前位置摘除并移到队尾。
static void touchAvatarLru(AvatarCache &cache, const QString &key)
{
    const int idx = cache.lruOrder.indexOf(key);
    if (idx >= 0) {
        cache.lruOrder.removeAt(idx);
        cache.lruOrder.append(key);
    }
}

// 根据头像 URL 读取文件并返回 Base64 字符串；[Avatar]/MaxCacheEntries>0 时走 LRU 缓存。
QString getAvaFromUrlStatic(const QString &savePath, const QString &url)
{
    if (url.isEmpty()) return QString();

    const QString avatarFullPath = QDir(savePath).filePath(QStringLiteral("ava") + QDir::separator() + url);
    const QString cacheKey = avatarFullPath;
    const int maxEntries = ServerConfigDefaults::avatarCacheMaxEntries();

    if (maxEntries > 0) {
        QMutexLocker lock(&s_avatarCacheMutex);
        AvatarCache &cache = getAvatarCache();
        const auto it = cache.hash.constFind(cacheKey);
        if (it != cache.hash.constEnd()) {
            touchAvatarLru(cache, cacheKey);
            return it.value();
        }
    }

    QFile avatarFile(avatarFullPath);
    if (!avatarFile.open(QIODevice::ReadOnly)) return QString();

    const QByteArray avatarData = avatarFile.readAll();
    avatarFile.close();
    const QString base64 = QString::fromLatin1(avatarData.toBase64());

    if (maxEntries > 0) {
        QMutexLocker lock(&s_avatarCacheMutex);
        AvatarCache &cache = getAvatarCache();
        while (cache.hash.size() >= maxEntries && !cache.lruOrder.isEmpty()) {
            const QString oldest = cache.lruOrder.takeFirst();
            cache.hash.remove(oldest);
        }
        // 并发下双 miss 可能重复插入同一 key：覆盖内容并刷新 LRU 序即可。
        if (cache.hash.contains(cacheKey)) {
            cache.hash.insert(cacheKey, base64);
            touchAvatarLru(cache, cacheKey);
            return base64;
        }
        cache.hash.insert(cacheKey, base64);
        cache.lruOrder.append(cacheKey);
    }
    return base64;
}

// ===== 协议解析 =====

// 解析 JSON 包，提取 tag 和 jsonObj。成功返回 true。
bool parseJsonPacket(const QByteArray &payload, QString &tag, QJsonObject &jsonObj)
{
    QJsonParseError parseError{};
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        tag.clear();
        jsonObj = QJsonObject();
        return false;
    }
    jsonObj = jsonDoc.object();
    tag = jsonObj.value("tag").toString();
    return true;
}

// 轻量提取 JSON 中的 tag 字段（仅用于背压时快速判定丢弃策略）。
// 成功返回 true，并将 tag 写入 tagOut。
bool tryExtractJsonTagFast(const QByteArray &payload, QString &tagOut)
{
    tagOut.clear();
    if (payload.isEmpty()) return false;

    // 非严格 JSON 扫描：寻找 "tag" : "xxx"
    const QByteArray key = "\"tag\"";
    int i = payload.indexOf(key);  // 直接字节查找，不解析JSON
    if (i < 0) return false;
    i += key.size();

    // 跳过空白字符
    while (i < payload.size()) {
        const char c = payload.at(i);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        break;
    }
    if (i >= payload.size() || payload.at(i) != ':') return false;
    ++i;

    // 再跳过空白
    while (i < payload.size()) {
        const char c = payload.at(i);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        break;
    }
    if (i >= payload.size() || payload.at(i) != '"') return false;
    ++i;

    // 开始读取 tag 字符串值
    QByteArray val;
    val.reserve(16);    // 预分配极小内存
    bool escape = false;
    for (; i < payload.size(); ++i) {
        const char c = payload.at(i);
        if (escape) {
            // 只处理常见转义，背压用途够用
            if (c == '"' || c == '\\' || c == '/') val.append(c);
            else if (c == 'b') val.append('\b');
            else if (c == 'f') val.append('\f');
            else if (c == 'n') val.append('\n');
            else if (c == 'r') val.append('\r');
            else if (c == 't') val.append('\t');
            else {
                // \uXXXX 等复杂转义：放弃 fast-path
                return false;
            }
            escape = false;
            continue;
        }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') break;  // 遇到结束引号，停止
        val.append(c);

        if (val.size() > 64) break; // tag 不会太长，防止恶意数据卡死
    }

    if (i >= payload.size()) return false;
    tagOut = QString::fromUtf8(val);
    return !tagOut.isEmpty();
}

// 解析二进制分片包，提取 uuid、seq、chunk。失败时 error 非空。
bool parseBinaryChunkPacket(const QByteArray &payload, QString &uuid, qint64 &seq,
                            QByteArray &chunk, QString &error)
{
    constexpr int kMinSize = 1 + 16 + 4 + 4;
    if (payload.size() < kMinSize) {
        error = QStringLiteral("binary payload too short");
        return false;
    }

    const uint8_t frameType = static_cast<uint8_t>(payload[0]);
    if (frameType != 1) {
        error = QStringLiteral("unsupported binary frame type");
        return false;
    }

    const QByteArray uuidBin = payload.mid(1, 16);
    const QUuid qu = QUuid::fromRfc4122(uuidBin);
    uuid = qu.toString(QUuid::WithoutBraces);
    if (uuid.isEmpty()) {
        error = QStringLiteral("invalid binary uuid");
        return false;
    }

    uint32_t seqNet = 0;
    memcpy(&seqNet, payload.constData() + 1 + 16, 4);
    seq = static_cast<qint64>(networkToHost32(seqNet));

    uint32_t lenNet = 0;
    memcpy(&lenNet, payload.constData() + 1 + 16 + 4, 4);
    const uint32_t chunkLen = networkToHost32(lenNet);
    const int headerLen = kMinSize;
    if (headerLen + static_cast<int>(chunkLen) > payload.size()) {
        error = QStringLiteral("binary chunk length invalid");
        return false;
    }

    chunk = payload.mid(headerLen, static_cast<int>(chunkLen));
    error.clear();
    return true;
}

// ===== Action 构造 =====

// 构造 send_self 类型 Action。
QJsonObject makeSendSelfAction(const QJsonObject &payload)
{
    QJsonObject action;
    action["type"] = "send_self";
    action["payload"] = payload;
    action["pre_encoded_payload_b64"] = QString::fromLatin1(
        QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64());
    return action;
}

// 构造 send_other 类型 Action。
QJsonObject makeSendOtherAction(const QString &account, const QJsonObject &payload)
{
    QJsonObject action;
    action["type"] = "send_other";
    action["account"] = account;
    action["payload"] = payload;
    action["pre_encoded_payload_b64"] = QString::fromLatin1(
        QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64());
    return action;
}

// 从已编码字节构造 Action，避免重复编码（当同一 payload 需同时 send_self 和 send_other 时使用）。
QJsonObject makeSendSelfActionFromEncoded(const QByteArray &encoded)
{
    QJsonObject action;
    action["type"] = "send_self";
    action["pre_encoded_payload_b64"] = QString::fromLatin1(encoded.toBase64());
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(encoded, &e);
    action["payload"] = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
    return action;
}

// 从已编码字节构造 send_other Action；优先复用 pre_encoded_payload_b64，避免重复 JSON 编码。
QJsonObject makeSendOtherActionFromEncoded(const QString &account, const QByteArray &encoded)
{
    QJsonObject action;
    action["type"] = "send_other";
    action["account"] = account;
    action["pre_encoded_payload_b64"] = QString::fromLatin1(encoded.toBase64());
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(encoded, &e);
    action["payload"] = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
    return action;
}

// 构造 ack 类型 Action。
QJsonObject makeAckAction(const QString &uuid, const QString &tag)
{
    QJsonObject action;
    action["type"] = "ack";
    action["tag"] = tag;
    action["uuid"] = uuid;
    QJsonObject ack;
    ack["tag"] = tag;
    ack["uuid"] = uuid;
    action["pre_encoded_payload_b64"] = QString::fromLatin1(
        QJsonDocument(ack).toJson(QJsonDocument::Compact).toBase64());
    return action;
}

// 将单个 payload 编码为 send_self Action 列表。
ClientHandler::WorkerActionList encodeSingleSendSelf(const QJsonObject &payload)
{
    ClientHandler::WorkerActionList actions;
    actions.append(makeSendSelfAction(payload));
    return actions;
}

// 将单个 payload 编码为 send_other Action 列表。
ClientHandler::WorkerActionList encodeSingleSendOther(const QString &account, const QJsonObject &payload)
{
    ClientHandler::WorkerActionList actions;
    actions.append(makeSendOtherAction(account, payload));
    return actions;
}

// 构造简单应答 Action（tag + answer）。
ClientHandler::WorkerActionList makeAnswerAction(const QString &tag, const QString &answer)
{
    QJsonObject payload;
    payload["tag"] = tag;
    payload["answer"] = answer;
    return encodeSingleSendSelf(payload);
}

// 将 Action 列表编码为可传输格式（当前实现直接返回）。
ClientHandler::WorkerActionList encodeActions(const ClientHandler::WorkerActionList &actions)
{
    return actions;
}

// 从 Action 列表中提取第一个 send_self 的 payload。
QJsonObject extractFirstSendSelfPayload(const ClientHandler::WorkerActionList &actions)
{
    for (const QJsonValue &v : actions) {
        if (!v.isObject()) continue;
        const QJsonObject action = v.toObject();
        if (action.value("type").toString() == "send_self" && action.value("payload").isObject()) {
            return action.value("payload").toObject();
        }
    }
    return QJsonObject();
}

// ===== 线程调度 =====

namespace {

// 线程池投递重试状态：用 QSharedPointer 共享，避免 lambda 捕获「指向自身的 shared_ptr<std::function>」形成环导致
// std::function + task/onDone + 上传 batch 等永不释放（表现为运存持续上涨）。
struct DbDispatchCtx {
    QPointer<QObject> context;
    std::function<QByteArray()> task;
    std::function<void(const QByteArray &)> onDone;
    qint64 dispatchStartMs = 0;
    int retryDelayMs = 0;
};

struct BusinessDispatchCtx {
    QPointer<QObject> context;
    std::function<void()> task;
    std::function<void()> onFinished;
    qint64 dispatchStartMs = 0;
    int retryDelayMs = 0;
};

// 尝试将 DB 任务投递到线程池：
// - 拿不到工作线程时按指数退避重试，超时后丢弃并回调空结果；
// - 投递成功后在线程池执行 task，再切回对象线程执行 onDone。
void attemptDbDispatch(const QSharedPointer<DbDispatchCtx> &ctx)
{
    if (!ctx || !ctx->context || !ctx->task) return;

    QThread *workerThread = ThreadPool::getInstance().getThread();
    if (!workerThread) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - ctx->dispatchStartMs;
        if (elapsed >= ServerConfigDefaults::dbDispatchMaxWaitMs()) {
            qWarning() << "【连接处理】线程池繁忙，丢弃 DB 任务";
            if (ctx->onDone) {
                QObject *c = ctx->context.data();
                auto onDone = ctx->onDone;
                runOnObjectThread(c, [onDone]() {
                    if (onDone) onDone(QByteArray());
                });
            }
            return;
        }
        const int waitMs = ctx->retryDelayMs;
        ctx->retryDelayMs = qMin(ServerConfigDefaults::defaultDbDispatchRetryMaxIntervalMs(), ctx->retryDelayMs * 2);
        scheduleRetry([ctx]() { attemptDbDispatch(ctx); }, waitMs);
        return;
    }

    auto *worker = new DbLambdaWorker(std::move(ctx->task));
    worker->moveToThread(workerThread);
    emitPerfMetric("threadpool_wait_ms", QDateTime::currentMSecsSinceEpoch() - ctx->dispatchStartMs);

    const QPointer<QObject> ctxPtr(ctx->context);
    const auto onDone = ctx->onDone;
    QObject::connect(worker, &DbLambdaWorker::resultReady, QCoreApplication::instance(),
        [worker, workerThread, ctxPtr, onDone](const QByteArray &result, qint64 dbMs) {
            ThreadPool::getInstance().releaseThread(workerThread);
            emitPerfMetric("db_ms", dbMs);
            if (ctxPtr) {
                runOnObjectThread(ctxPtr.data(), [onDone, result]() {
                    if (onDone) onDone(result);
                });
            }
            worker->deleteLater();
        }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(worker, &DbLambdaWorker::process, Qt::QueuedConnection);
}

// 尝试将通用业务任务投递到线程池：
// - 调度与退避策略与 DB 任务一致；
// - 任务本体无返回值，完成后仅回对象线程触发 onFinished。
void attemptBusinessDispatch(const QSharedPointer<BusinessDispatchCtx> &ctx)
{
    if (!ctx || !ctx->context || !ctx->task) return;

    QThread *workerThread = ThreadPool::getInstance().getThread();
    if (!workerThread) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - ctx->dispatchStartMs;
        if (elapsed >= ServerConfigDefaults::dbDispatchMaxWaitMs()) {
            qWarning() << "【连接处理】线程池繁忙，丢弃业务任务";
            if (ctx->onFinished) {
                QObject *c = ctx->context.data();
                auto onFinished = ctx->onFinished;
                runOnObjectThread(c, [onFinished]() {
                    if (onFinished) onFinished();
                });
            }
            return;
        }
        const int waitMs = ctx->retryDelayMs;
        ctx->retryDelayMs = qMin(ServerConfigDefaults::defaultDbDispatchRetryMaxIntervalMs(), ctx->retryDelayMs * 2);
        scheduleRetry([ctx]() { attemptBusinessDispatch(ctx); }, waitMs);
        return;
    }

    auto *worker = new DbLambdaWorker([task = std::move(ctx->task)]() -> QByteArray {
        if (task) task();
        return QByteArray("1");
    });
    worker->moveToThread(workerThread);
    emitPerfMetric("threadpool_wait_ms", QDateTime::currentMSecsSinceEpoch() - ctx->dispatchStartMs);

    const QPointer<QObject> ctxPtr(ctx->context);
    const auto onFinished = ctx->onFinished;
    QObject::connect(worker, &DbLambdaWorker::resultReady, QCoreApplication::instance(),
        [worker, workerThread, ctxPtr, onFinished](const QByteArray &, qint64 dbMs) {
            ThreadPool::getInstance().releaseThread(workerThread);
            emitPerfMetric("db_ms", dbMs);
            if (ctxPtr) {
                runOnObjectThread(ctxPtr.data(), [onFinished]() {
                    if (onFinished) onFinished();
                });
            }
            worker->deleteLater();
        }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(worker, &DbLambdaWorker::process, Qt::QueuedConnection);
}

} // namespace

// runDbTask 内部实现。
void runDbTaskImpl(QObject *context, std::function<QByteArray()> task, std::function<void(const QByteArray &)> onDone)
{
    if (!context || !task) return;

    auto ctx = QSharedPointer<DbDispatchCtx>::create();
    ctx->context = context;
    ctx->task = std::move(task);
    ctx->onDone = std::move(onDone);
    ctx->dispatchStartMs = QDateTime::currentMSecsSinceEpoch();
    ctx->retryDelayMs = ServerConfigDefaults::defaultDbDispatchRetryIntervalMs();

    attemptDbDispatch(ctx);
}

// 将业务任务投递到线程池执行，可选 onFinished 回调。
void dispatchBusinessTaskToThreadPool(QObject *context,
                                      std::function<void()> task,
                                      std::function<void()> onFinished)
{
    if (!context) return;

    auto ctx = QSharedPointer<BusinessDispatchCtx>::create();
    ctx->context = context;
    ctx->task = std::move(task);
    ctx->onFinished = std::move(onFinished);
    ctx->dispatchStartMs = QDateTime::currentMSecsSinceEpoch();
    ctx->retryDelayMs = ServerConfigDefaults::defaultDbDispatchRetryIntervalMs();

    attemptBusinessDispatch(ctx);
}

// 统一重试调度：将任务加入重试队列，由单一定时器驱动。delayMs<0 使用 defaultDbDispatchRetryIntervalMs。
void scheduleRetry(std::function<void()> fn, int delayMs)
{
    if (delayMs < 0) delayMs = ServerConfigDefaults::defaultDbDispatchRetryIntervalMs();
    TaskRetryQueue::instance().schedule(std::move(fn), delayMs);
}

// 在线程池中异步删除文件。
void scheduleFileRemoveOnThreadPool(QObject *context, const QString &path)
{
    if (!context || path.isEmpty()) return;
    dispatchBusinessTaskToThreadPool(context, [path]() { QFile::remove(path); });
}

}  // namespace ClientHandlerShared
