/**
 * @file ServerConfigDefaults.cpp
 * ServerConfigDefaults 实现，详见 ServerConfigDefaults.h。
 */
#include "ServerConfigDefaults.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <mutex>
#include <utility>

namespace ServerConfigDefaults {

// 获取数据根目录（AppImage 环境 → $HOME/.suliao-server/，否则 applicationDirPath/）。
QString dataDir()
{
    const QString appDirEnv = qEnvironmentVariable("APPDIR");
    if (!appDirEnv.isEmpty()) {
        return QDir::homePath() + QDir::separator() + QLatin1String(".suliao-server") + QDir::separator();
    }
    return QCoreApplication::applicationDirPath() + QDir::separator();
}

// 获取配置文件路径（基于 dataDir()）。
QString settingsIniPath()
{
    return dataDir() + QLatin1String("Settings.ini");
}

// 获取存储根目录（基于 dataDir() + storage/）。
QString storageBasePath()
{
    return dataDir() + QLatin1String("storage") + QDir::separator();
}

namespace {

struct CachedConfig {
    // Server
    quint16 listenPort = 0;
    int clientIdleTimeoutMs = 0;
    int rateLimitWindowMs = 0;
    int heartMaxPerWindow = 0;
    int unknownTagMaxPerWindow = 0;
    bool disconnectOnUnknownTagLimit = true;
    QSet<QString> backpressureDroppableTagSet;  // 仅保留 QSet，按需生成 QStringList

    // ThreadPool
    int threadPoolMaxThreads = 0;
    int threadPoolIdleTimeoutMs = 0;
    int threadPoolCheckIntervalMs = 0;

    // DatabasePool
    int databasePoolMaxConnections = 0;
    bool databasePoolAllowSetGlobalMaxConnections = false;

    // Log
    int noisyLogSampleEvery = 0;

    // ClientHandler 队列限制（从 Settings.ini 读取，便于调优）
    int docSendQueueMaxPackets = 0;
    int docSendQueueMaxBytes = 0;
    int dbDispatchMaxWaitMs = 0;  // 来自 ini；≤0 时在 dbDispatchMaxWaitMs() 中回退到 defaultDbDispatchMaxWaitMs()
    int jsonParseQueueMaxPackets = 0;
    int jsonParseQueueMaxBytes = 0;
    int outQueueMaxPackets = 0;
    int outQueueMaxBytes = 0;
    int binaryParseQueueMaxPackets = 0;
    int binaryParseQueueMaxBytes = 0;
    // ClientHandler 传输（与 default* 一致后由 ini 覆盖）
    int docChunkBytes = 0;
    int uploadChunkBytes = 0;
    bool docRateLimitEnabled = false;
    int docRateBytesPerSec = 0;
    int docBurstBytes = 0;
    int docSendPostBatchChunks = 0;
    int docSendMaxMsPerSlice = 0;
    qint64 socketReadSlackBytes = 0;
    qint64 socketWriteHighWaterBytes = 0;

    // Avatar LRU（0=禁用）
    int avatarCacheMaxEntries = 0;
};

static CachedConfig g_cache;
static std::once_flag g_defaultsOnce;
static std::once_flag g_cacheOnce;

static QStringList parseCsvTagsToList(const QString &csv)
{
    QStringList parts = csv.split(",", Qt::SkipEmptyParts);
    for (QString &p : parts) p = p.trimmed();
    parts.removeAll(QString());
    // 去重（保序），避免重复 tag 造成不必要的 set 插入/列表膨胀。
    QSet<QString> seen;
    QStringList uniq;
    uniq.reserve(parts.size());
    for (const QString &p : parts) {
        if (seen.contains(p)) continue;
        seen.insert(p);
        uniq.push_back(p);
    }
    return uniq;
}

static void loadCacheOnce()
{
    // 先确保 ini 已补齐缺省键，再读取缓存。
    ServerConfigDefaults::ensureServerDefaults();

    CachedConfig c;
    c.listenPort = ServerConfigDefaults::defaultListenPort();
    c.clientIdleTimeoutMs = ServerConfigDefaults::defaultClientIdleTimeoutMs();
    c.rateLimitWindowMs = ServerConfigDefaults::defaultRateLimitWindowMs();
    c.heartMaxPerWindow = ServerConfigDefaults::defaultHeartMaxPerWindow();
    c.unknownTagMaxPerWindow = ServerConfigDefaults::defaultUnknownTagMaxPerWindow();
    c.disconnectOnUnknownTagLimit = ServerConfigDefaults::defaultDisconnectOnUnknownTagLimit();
    c.threadPoolMaxThreads = ServerConfigDefaults::defaultThreadPoolMaxThreads();
    c.threadPoolIdleTimeoutMs = ServerConfigDefaults::defaultThreadPoolIdleTimeoutMs();
    c.threadPoolCheckIntervalMs = ServerConfigDefaults::defaultThreadPoolCheckIntervalMs();
    c.databasePoolMaxConnections = ServerConfigDefaults::defaultMaxDbConnections();
    c.databasePoolAllowSetGlobalMaxConnections = false;
    c.noisyLogSampleEvery = ServerConfigDefaults::defaultNoisyLogSampleEvery();
    c.docSendQueueMaxPackets = ServerConfigDefaults::defaultDocSendQueueMaxPackets();
    c.docSendQueueMaxBytes = ServerConfigDefaults::defaultDocSendQueueMaxBytes();
    c.jsonParseQueueMaxPackets = ServerConfigDefaults::defaultJsonParseQueueMaxPackets();
    c.jsonParseQueueMaxBytes = ServerConfigDefaults::defaultJsonParseQueueMaxBytes();
    c.outQueueMaxPackets = ServerConfigDefaults::defaultOutQueueMaxPackets();
    c.outQueueMaxBytes = ServerConfigDefaults::defaultOutQueueMaxBytes();
    c.binaryParseQueueMaxPackets = ServerConfigDefaults::defaultBinaryParseQueueMaxPackets();
    c.binaryParseQueueMaxBytes = ServerConfigDefaults::defaultBinaryParseQueueMaxBytes();
    c.dbDispatchMaxWaitMs = ServerConfigDefaults::defaultDbDispatchMaxWaitMs();
    c.docChunkBytes = ServerConfigDefaults::defaultDocChunkBytes();
    c.uploadChunkBytes = ServerConfigDefaults::defaultUploadChunkBytes();
    c.docRateLimitEnabled = ServerConfigDefaults::defaultDocRateLimitEnabled();
    c.docRateBytesPerSec = ServerConfigDefaults::defaultDocRateBytesPerSec();
    c.docBurstBytes = ServerConfigDefaults::defaultDocBurstBytes();
    c.docSendPostBatchChunks = ServerConfigDefaults::defaultDocSendPostBatchChunks();
    c.docSendMaxMsPerSlice = ServerConfigDefaults::defaultDocSendMaxMsPerSlice();
    c.socketReadSlackBytes = ServerConfigDefaults::defaultSocketReadSlackBytes();
    c.socketWriteHighWaterBytes = ServerConfigDefaults::defaultSocketWriteHighWaterBytes();
    c.avatarCacheMaxEntries = ServerConfigDefaults::defaultAvatarCacheMaxCost();

    QSettings s(ServerConfigDefaults::settingsIniPath(), QSettings::IniFormat);

    s.beginGroup("Server");
    {
        const int v = s.value("ListenPort", static_cast<int>(c.listenPort)).toInt();
        if (v > 0 && v <= 65535) c.listenPort = static_cast<quint16>(v);
    }
    {
        const int v = s.value("ClientIdleTimeoutMs", c.clientIdleTimeoutMs).toInt();
        c.clientIdleTimeoutMs = v > 0 ? v : c.clientIdleTimeoutMs;
    }
    {
        const int v = s.value("RateLimitWindowMs", c.rateLimitWindowMs).toInt();
        c.rateLimitWindowMs = v > 0 ? v : c.rateLimitWindowMs;
    }
    {
        const int v = s.value("HeartMaxPerWindow", c.heartMaxPerWindow).toInt();
        c.heartMaxPerWindow = v >= 0 ? v : c.heartMaxPerWindow;
    }
    {
        const int v = s.value("UnknownTagMaxPerWindow", c.unknownTagMaxPerWindow).toInt();
        c.unknownTagMaxPerWindow = v >= 0 ? v : c.unknownTagMaxPerWindow;
    }
    c.disconnectOnUnknownTagLimit = s.value("DisconnectOnUnknownTagLimit", c.disconnectOnUnknownTagLimit).toBool();
    {
        const QString csv = s.value("BackpressureDroppableTags", ServerConfigDefaults::defaultBackpressureDroppableTags()).toString();
        const QStringList list = parseCsvTagsToList(csv);
        c.backpressureDroppableTagSet = QSet<QString>(list.cbegin(), list.cend());
    }
    s.endGroup();

    s.beginGroup("ThreadPool");
    {
        const int v = s.value("MaxThreads", c.threadPoolMaxThreads).toInt();
        c.threadPoolMaxThreads = v > 0 ? v : c.threadPoolMaxThreads;
    }
    {
        const int v = s.value("IdleTimeoutMs", c.threadPoolIdleTimeoutMs).toInt();
        c.threadPoolIdleTimeoutMs = v > 0 ? v : c.threadPoolIdleTimeoutMs;
    }
    {
        const int v = s.value("CheckIntervalMs", c.threadPoolCheckIntervalMs).toInt();
        c.threadPoolCheckIntervalMs = v > 0 ? v : c.threadPoolCheckIntervalMs;
    }
    s.endGroup();

    s.beginGroup("DatabasePool");
    {
        const int v = s.value("MaxConnections", c.databasePoolMaxConnections).toInt();
        c.databasePoolMaxConnections = v > 0 ? v : c.databasePoolMaxConnections;
    }
    c.databasePoolAllowSetGlobalMaxConnections = s.value("AllowSetGlobalMaxConnections", false).toBool();
    s.endGroup();

    s.beginGroup("Log");
    {
        int v = s.value("NoisyLogSampleEvery", c.noisyLogSampleEvery).toInt();
        if (v < 0) v = 0;
        c.noisyLogSampleEvery = v;
    }
    s.endGroup();

    s.beginGroup("ClientHandler");
    {
        const int v1 = s.value("DocSendQueueMaxPackets", c.docSendQueueMaxPackets).toInt();
        c.docSendQueueMaxPackets = v1 > 0 ? v1 : c.docSendQueueMaxPackets;
    }
    {
        const int v2 = s.value("DocSendQueueMaxBytes", c.docSendQueueMaxBytes).toInt();
        c.docSendQueueMaxBytes = v2 > 0 ? v2 : c.docSendQueueMaxBytes;
    }
    {
        const int v3 = s.value("JsonParseQueueMaxPackets", c.jsonParseQueueMaxPackets).toInt();
        c.jsonParseQueueMaxPackets = v3 > 0 ? v3 : c.jsonParseQueueMaxPackets;
    }
    {
        const int v4 = s.value("JsonParseQueueMaxBytes", c.jsonParseQueueMaxBytes).toInt();
        c.jsonParseQueueMaxBytes = v4 > 0 ? v4 : c.jsonParseQueueMaxBytes;
    }
    {
        const int v5 = s.value("OutQueueMaxPackets", c.outQueueMaxPackets).toInt();
        c.outQueueMaxPackets = v5 > 0 ? v5 : c.outQueueMaxPackets;
    }
    {
        const int v6 = s.value("OutQueueMaxBytes", c.outQueueMaxBytes).toInt();
        c.outQueueMaxBytes = v6 > 0 ? v6 : c.outQueueMaxBytes;
    }
    {
        const int v7 = s.value("BinaryParseQueueMaxPackets", c.binaryParseQueueMaxPackets).toInt();
        c.binaryParseQueueMaxPackets = v7 > 0 ? v7 : c.binaryParseQueueMaxPackets;
    }
    {
        const int v8 = s.value("BinaryParseQueueMaxBytes", c.binaryParseQueueMaxBytes).toInt();
        c.binaryParseQueueMaxBytes = v8 > 0 ? v8 : c.binaryParseQueueMaxBytes;
    }
    {
        const int v9 = s.value("DbDispatchMaxWaitMs", c.dbDispatchMaxWaitMs).toInt();
        c.dbDispatchMaxWaitMs = v9 > 0 ? v9 : c.dbDispatchMaxWaitMs;
    }
    {
        const int v = s.value("DocChunkBytes", c.docChunkBytes).toInt();
        c.docChunkBytes = (v > 0 && v <= (16 * 1024 * 1024)) ? v : c.docChunkBytes;
    }
    {
        const int v = s.value("UploadChunkBytes", c.uploadChunkBytes).toInt();
        c.uploadChunkBytes = (v > 0 && v <= (16 * 1024 * 1024)) ? v : c.uploadChunkBytes;
    }
    {
        if (s.contains("RateLimitEnabled")) {
            c.docRateLimitEnabled = s.value("RateLimitEnabled", c.docRateLimitEnabled).toBool();
        }
    }
    {
        const int v = s.value("DocRateBytesPerSec", c.docRateBytesPerSec).toInt();
        c.docRateBytesPerSec = v > 0 ? v : c.docRateBytesPerSec;
    }
    {
        const int v = s.value("DocBurstBytes", c.docBurstBytes).toInt();
        c.docBurstBytes = v > 0 ? v : c.docBurstBytes;
    }
    {
        const int v = s.value("DocSendPostBatchChunks", c.docSendPostBatchChunks).toInt();
        c.docSendPostBatchChunks = (v >= 1 && v <= 128) ? v : c.docSendPostBatchChunks;
    }
    {
        const int v = s.value("DocSendMaxMsPerSlice", c.docSendMaxMsPerSlice).toInt();
        c.docSendMaxMsPerSlice = (v > 0 && v <= 5000) ? v : c.docSendMaxMsPerSlice;
    }
    {
        const qint64 v = static_cast<qint64>(s.value("SocketReadSlackBytes", c.socketReadSlackBytes).toLongLong());
        c.socketReadSlackBytes = (v >= 0 && v <= 65536) ? v : c.socketReadSlackBytes;
    }
    {
        const qint64 v = static_cast<qint64>(s.value("SocketWriteHighWaterBytes", c.socketWriteHighWaterBytes).toLongLong());
        constexpr qint64 kMinW = 65536;                     // 64 KiB
        constexpr qint64 kMaxW = 512LL * 1024 * 1024;       // 512 MiB
        c.socketWriteHighWaterBytes = (v >= kMinW && v <= kMaxW) ? v : c.socketWriteHighWaterBytes;
    }
    s.endGroup();

    s.beginGroup("Avatar");
    {
        const int v = s.value("MaxCacheEntries", c.avatarCacheMaxEntries).toInt();
        c.avatarCacheMaxEntries = v >= 0 ? v : 0;
    }
    s.endGroup();

    g_cache = std::move(c);
}

static const CachedConfig &cache()
{
    std::call_once(g_cacheOnce, []() { loadCacheOnce(); });
    return g_cache;
}

} // namespace

// ===== Server 基础 =====
// 默认监听端口。
quint16 defaultListenPort() { return 20001; }
// 默认数据库连接池最大连接数。
int defaultMaxDbConnections() { return 128; }
// 默认客户端空闲超时（毫秒）。
int defaultClientIdleTimeoutMs() { return 60000; }
// 默认停止检测间隔（毫秒）。
int defaultStopCheckIntervalMs() { return 5000; }
// 默认背压时可丢弃的 tag 列表（逗号分隔）。
QString defaultBackpressureDroppableTags() { return "heart,pong"; }
// 默认限流窗口长度（毫秒）。
int defaultRateLimitWindowMs() { return 1000; }
// 默认每窗口允许的 heart 最大次数（超出将丢弃）。
int defaultHeartMaxPerWindow() { return 100; }
// 默认每窗口允许的未知 tag 最大次数（超出将丢弃或断开）。
int defaultUnknownTagMaxPerWindow() { return 50; }
// 默认未知 tag 超限时是否断开连接。
bool defaultDisconnectOnUnknownTagLimit() { return true; }

// 返回缓存的监听端口（ensureServerDefaults + loadCacheOnce 之后有效）。
quint16 serverListenPort()
{
    return cache().listenPort;
}

// 返回缓存的客户端空闲超时毫秒值。
int serverClientIdleTimeoutMs()
{
    return cache().clientIdleTimeoutMs;
}

// 返回背压时可丢弃的 tag 集合（进程内只读缓存）。
const QSet<QString> &serverBackpressureDroppableTagSet()
{
    return cache().backpressureDroppableTagSet;
}

// 从 Settings.ini 读取限流窗口长度（毫秒，带兜底默认值）。
int serverRateLimitWindowMs()
{
    return cache().rateLimitWindowMs;
}

// 从 Settings.ini 读取每窗口允许的 heart 最大次数（带兜底默认值）。
int serverHeartMaxPerWindow()
{
    return cache().heartMaxPerWindow;
}

// 从 Settings.ini 读取每窗口允许的未知 tag 最大次数（带兜底默认值）。
int serverUnknownTagMaxPerWindow()
{
    return cache().unknownTagMaxPerWindow;
}

// 从 Settings.ini 读取未知 tag 超限时是否断开连接（带兜底默认值）。
bool serverDisconnectOnUnknownTagLimit()
{
    return cache().disconnectOnUnknownTagLimit;
}

// 返回缓存的业务线程池最大线程数。
int threadPoolMaxThreads()
{
    return cache().threadPoolMaxThreads;
}

// 返回缓存的线程池空闲超时（毫秒）。
int threadPoolIdleTimeoutMs()
{
    return cache().threadPoolIdleTimeoutMs;
}

// 返回缓存的线程池空闲检查间隔（毫秒）。
int threadPoolCheckIntervalMs()
{
    return cache().threadPoolCheckIntervalMs;
}

// 返回缓存的数据库连接池最大连接数。
int databasePoolMaxConnections()
{
    return cache().databasePoolMaxConnections;
}

// ===== ServerTimers =====
// 默认空闲检查定时器间隔（毫秒）。
int defaultIdleCheckIntervalMs() { return 5000; }
// 默认文档发送泵送定时器间隔（毫秒）。
int defaultDocSendIntervalMs() { return 10; }
// 默认上传会话清理定时器间隔（毫秒）。
int defaultUploadCleanupIntervalMs() { return 30000; }

// ===== 数据库 =====
// 默认数据库主机地址。
QString defaultDbHost() { return "localhost"; }
// 默认数据库端口。
int defaultDbPort() { return 3306; }
// 默认数据库名称。
QString defaultDbName() { return "suliaoserver"; }
// 默认数据库用户名。
QString defaultDbUser() { return "root"; }
// 默认数据库密码。
QString defaultDbPassword() { return "123456"; }
// 默认数据库连接建立超时（秒）。
int defaultDatabasePoolConnConnectTimeoutSec() { return 15; }
// ===== 线程池 =====
// 默认线程池最大线程数。
int defaultThreadPoolMaxThreads() { return 120; }
// 默认线程空闲超时（毫秒）。
int defaultThreadPoolIdleTimeoutMs() { return 30 * 60 * 1000; }
// 默认线程空闲检查间隔（毫秒）。
int defaultThreadPoolCheckIntervalMs() { return 10 * 60 * 1000; }

// ===== ClientHandler 传输 =====
// 默认文档下载分片大小（字节）。
int defaultDocChunkBytes() { return 64 * 1024; }
// 默认上传分片大小（字节）。
int defaultUploadChunkBytes() { return 64 * 1024; }
// 默认启用下载限速。
bool defaultDocRateLimitEnabled() { return true; }
// 默认文档下发速率限制（字节/秒）。
int defaultDocRateBytesPerSec() { return 8 * 1024 * 1024; }
// 默认文档下发突发额度（字节）：小窗口，避免瞬间塞满发送队列。
int defaultDocBurstBytes() { return 4 * 1024 * 1024; }
// 默认文档发送队列包数上限（大文件按流式窗口发送，不需万级包数）。
int defaultDocSendQueueMaxPackets() { return 4096; }
// 默认文档发送队列字节上限（约 32 MiB 滑动窗口，与 Settings.ini 一致）。
int defaultDocSendQueueMaxBytes() { return 32 * 1024 * 1024; }
// 默认单连接最大并发上传数。
int defaultMaxConcurrentUploadsPerConn() { return 3; }
// 默认上传会话空闲超时（毫秒）。
qint64 defaultUploadIdleTimeoutMs() { return 60 * 1000; }

// ===== ClientHandler 队列与时间片 =====
// 默认 readyRead 单次处理最大包数。
int defaultReadyReadMaxPacketsPerSlice() { return 64; }
// 默认 readyRead 单次处理最大耗时（毫秒）。
qint64 defaultReadyReadMaxMsPerSlice() { return 2; }
// 默认文档发送单次时间片（毫秒）。
int defaultDocSendMaxMsPerSlice() { return 2; }
// 默认上传写盘单批最大分片数。
int defaultUploadWriteBatchMaxChunks() { return 8; }
// 默认上传写盘单批最大字节数。
int defaultUploadWriteBatchMaxBytes() { return 512 * 1024; }
// 默认转发单批最大条目数。
int defaultForwardBatchItemsPerSlice() { return 64; }
// 默认 JSON 解析队列包数上限（以处理能力为主）。
int defaultJsonParseQueueMaxPackets() { return 1024; }
// 默认 JSON 解析队列字节上限。
int defaultJsonParseQueueMaxBytes() { return 16 * 1024 * 1024; }
// 默认单次 JSON 解析批处理最大包数。
int defaultJsonParseBatchMaxPackets() { return 32; }
// 默认单次 JSON 解析批处理最大字节数。
int defaultJsonParseBatchMaxBytes() { return 512 * 1024; }
// 默认二进制解析队列包数上限（以处理能力为主）。
int defaultBinaryParseQueueMaxPackets() { return 2048; }
// 默认二进制解析队列字节上限（大文件上传接收侧缓冲）。
int defaultBinaryParseQueueMaxBytes() { return 256 * 1024 * 1024; }
// 默认原始发送队列包数上限。
int defaultOutQueueMaxPackets() { return 4096; }
// 默认原始发送队列字节上限（约 32 MiB，与 doc 队列组成双级背压窗口）。
int defaultOutQueueMaxBytes() { return 32 * 1024 * 1024; }
// 默认 socket 写缓冲高水位（字节）：超过则暂停继续入队。
qint64 defaultSocketWriteHighWaterBytes() { return 8 * 1024 * 1024; }
// 与 PROTOCOL_HEADER_LEN * 16 对齐（8*16=128），避免 ServerConfigDefaults 依赖协议头文件。
int defaultDocSendPostBatchChunks() { return 8; }
qint64 defaultSocketReadSlackBytes() { return 128; }

// ===== ClientHandlerShared =====
// 默认线程池调度重试初始间隔（毫秒）。
int defaultDbDispatchRetryIntervalMs() { return 2; }
// 默认线程池调度重试间隔上限（毫秒）。
int defaultDbDispatchRetryMaxIntervalMs() { return 64; }
// 默认线程池调度最大等待时间（毫秒，以处理能力为主）。
int defaultDbDispatchMaxWaitMs() { return 20000; }
// 返回线程池调度最大等待毫秒（ini 无效时回退 defaultDbDispatchMaxWaitMs）。
int dbDispatchMaxWaitMs() {
    const int v = cache().dbDispatchMaxWaitMs;
    return v > 0 ? v : defaultDbDispatchMaxWaitMs();
}
// 默认日志采样周期（每 N 次输出一次）。
quint64 defaultShouldSampleEvery() { return 128; }
// 默认头像缓存最大条目数（0=禁用，由 [Avatar]/MaxCacheEntries 配置）。
int defaultAvatarCacheMaxCost() { return 0; }

// 返回头像 LRU 最大条数（0 表示禁用缓存）。
int avatarCacheMaxEntries()
{
    return cache().avatarCacheMaxEntries;
}

// ===== 日志 =====
// 默认日志级别（debug/info/warning/error/critical/fatal）。
QString defaultLogLevel() { return "info"; }
// 默认是否每行刷盘。
bool defaultLogFlushEveryLine() { return false; }
// 默认是否输出到控制台。
bool defaultLogConsoleOutput() { return true; }
// 默认单日志文件最大大小（MB），0 表示不轮转。
int defaultLogMaxFileSizeMB() { return 50; }
// 默认轮转保留文件数量。
int defaultLogFileCount() { return 10; }
// 默认 PERF 指标采样（0=关闭，1=全量，N=每 N 次采样）。
int defaultPerfMetricSampleEvery() { return 0; }
// 默认噪声/高频日志采样（0=关闭，1=全量，N=每 N 次采样）。
int defaultNoisyLogSampleEvery() { return 0; }

// 返回噪声/高频日志采样周期（与头文件 logNoisyLogSampleEvery 一致）。
int logNoisyLogSampleEvery()
{
    return cache().noisyLogSampleEvery;
}

// 返回是否允许调整 MySQL 全局 max_connections（ini 缓存值）。
bool databasePoolAllowSetGlobalMaxConnections()
{
    return cache().databasePoolAllowSetGlobalMaxConnections;
}

// ===== ClientHandler 队列与传输（从 Settings.ini 缓存读取，与头文件声明一致）=====
int serverDocSendQueueMaxPackets() { return cache().docSendQueueMaxPackets; }
int serverDocSendQueueMaxBytes() { return cache().docSendQueueMaxBytes; }
int serverJsonParseQueueMaxPackets() { return cache().jsonParseQueueMaxPackets; }
int serverJsonParseQueueMaxBytes() { return cache().jsonParseQueueMaxBytes; }
int serverOutQueueMaxPackets() { return cache().outQueueMaxPackets; }
int serverOutQueueMaxBytes() { return cache().outQueueMaxBytes; }
int serverBinaryParseQueueMaxPackets() { return cache().binaryParseQueueMaxPackets; }
int serverBinaryParseQueueMaxBytes() { return cache().binaryParseQueueMaxBytes; }
int serverDocChunkBytes() { return cache().docChunkBytes; }
int serverUploadChunkBytes() { return cache().uploadChunkBytes; }
bool serverDocRateLimitEnabled() { return cache().docRateLimitEnabled; }
int serverDocRateBytesPerSec() { return cache().docRateBytesPerSec; }
int serverDocBurstBytes() { return cache().docBurstBytes; }
int serverDocSendPostBatchChunks() { return cache().docSendPostBatchChunks; }
int serverDocSendMaxMsPerSlice() { return cache().docSendMaxMsPerSlice; }
qint64 serverSocketReadSlackBytes() { return cache().socketReadSlackBytes; }
qint64 serverSocketWriteHighWaterBytes() { return cache().socketWriteHighWaterBytes; }

// 写入缺失默认配置。
void ensureServerDefaults()
{
    std::call_once(g_defaultsOnce, []() {
        const QString path = settingsIniPath();
        QSettings def(path, QSettings::IniFormat);

        def.beginGroup("Server");
        if (!def.contains("ListenPort")) def.setValue("ListenPort", defaultListenPort());
        if (!def.contains("ClientIdleTimeoutMs")) def.setValue("ClientIdleTimeoutMs", defaultClientIdleTimeoutMs());
        if (!def.contains("StopCheckIntervalMs")) def.setValue("StopCheckIntervalMs", defaultStopCheckIntervalMs());
        if (!def.contains("BackpressureDroppableTags")) def.setValue("BackpressureDroppableTags", defaultBackpressureDroppableTags());
        if (!def.contains("RateLimitWindowMs")) def.setValue("RateLimitWindowMs", defaultRateLimitWindowMs());
        if (!def.contains("HeartMaxPerWindow")) def.setValue("HeartMaxPerWindow", defaultHeartMaxPerWindow());
        if (!def.contains("UnknownTagMaxPerWindow")) def.setValue("UnknownTagMaxPerWindow", defaultUnknownTagMaxPerWindow());
        if (!def.contains("DisconnectOnUnknownTagLimit")) def.setValue("DisconnectOnUnknownTagLimit", defaultDisconnectOnUnknownTagLimit());
        def.endGroup();

        def.beginGroup("Database");
        if (!def.contains("Host")) def.setValue("Host", defaultDbHost());
        if (!def.contains("Port")) def.setValue("Port", defaultDbPort());
        if (!def.contains("Database")) def.setValue("Database", defaultDbName());
        if (!def.contains("User")) def.setValue("User", defaultDbUser());
        if (!def.contains("Password")) def.setValue("Password", defaultDbPassword());
        def.endGroup();

        // 写入连接池参数
        def.beginGroup("DatabasePool");
        if (!def.contains("MaxConnections")) def.setValue("MaxConnections", defaultMaxDbConnections());
        if (!def.contains("ConnConnectTimeoutSec")) {
            def.setValue("ConnConnectTimeoutSec", defaultDatabasePoolConnConnectTimeoutSec());
        }
        if (!def.contains("AllowSetGlobalMaxConnections")) def.setValue("AllowSetGlobalMaxConnections", false);
        def.endGroup();

        // 写入线程池参数
        def.beginGroup("ThreadPool");
        if (!def.contains("MaxThreads")) def.setValue("MaxThreads", defaultThreadPoolMaxThreads());
        if (!def.contains("IdleTimeoutMs")) def.setValue("IdleTimeoutMs", defaultThreadPoolIdleTimeoutMs());
        if (!def.contains("CheckIntervalMs")) def.setValue("CheckIntervalMs", defaultThreadPoolCheckIntervalMs());
        def.endGroup();

        // 写入日志参数
        def.beginGroup("Log");
        if (!def.contains("Level")) def.setValue("Level", defaultLogLevel());
        if (!def.contains("FlushEveryLine")) def.setValue("FlushEveryLine", defaultLogFlushEveryLine());
        if (!def.contains("ConsoleOutput")) def.setValue("ConsoleOutput", defaultLogConsoleOutput());
        if (!def.contains("MaxFileSizeMB")) def.setValue("MaxFileSizeMB", defaultLogMaxFileSizeMB());
        if (!def.contains("FileCount")) def.setValue("FileCount", defaultLogFileCount());
        if (!def.contains("PerfMetricSampleEvery")) def.setValue("PerfMetricSampleEvery", defaultPerfMetricSampleEvery());
        if (!def.contains("NoisyLogSampleEvery")) def.setValue("NoisyLogSampleEvery", defaultNoisyLogSampleEvery());
        def.endGroup();

        def.beginGroup("ClientHandler");
        if (!def.contains("DocSendQueueMaxPackets")) def.setValue("DocSendQueueMaxPackets", defaultDocSendQueueMaxPackets());
        if (!def.contains("DocSendQueueMaxBytes")) def.setValue("DocSendQueueMaxBytes", defaultDocSendQueueMaxBytes());
        if (!def.contains("JsonParseQueueMaxPackets")) def.setValue("JsonParseQueueMaxPackets", defaultJsonParseQueueMaxPackets());
        if (!def.contains("JsonParseQueueMaxBytes")) def.setValue("JsonParseQueueMaxBytes", defaultJsonParseQueueMaxBytes());
        if (!def.contains("OutQueueMaxPackets")) def.setValue("OutQueueMaxPackets", defaultOutQueueMaxPackets());
        if (!def.contains("OutQueueMaxBytes")) def.setValue("OutQueueMaxBytes", defaultOutQueueMaxBytes());
        if (!def.contains("BinaryParseQueueMaxPackets")) def.setValue("BinaryParseQueueMaxPackets", defaultBinaryParseQueueMaxPackets());
        if (!def.contains("BinaryParseQueueMaxBytes")) def.setValue("BinaryParseQueueMaxBytes", defaultBinaryParseQueueMaxBytes());
        if (!def.contains("DbDispatchMaxWaitMs")) def.setValue("DbDispatchMaxWaitMs", defaultDbDispatchMaxWaitMs());
        if (!def.contains("DocChunkBytes")) def.setValue("DocChunkBytes", defaultDocChunkBytes());
        if (!def.contains("UploadChunkBytes")) def.setValue("UploadChunkBytes", defaultUploadChunkBytes());
        if (!def.contains("RateLimitEnabled")) def.setValue("RateLimitEnabled", defaultDocRateLimitEnabled());
        if (!def.contains("DocRateBytesPerSec")) def.setValue("DocRateBytesPerSec", defaultDocRateBytesPerSec());
        if (!def.contains("DocBurstBytes")) def.setValue("DocBurstBytes", defaultDocBurstBytes());
        if (!def.contains("DocSendPostBatchChunks")) def.setValue("DocSendPostBatchChunks", defaultDocSendPostBatchChunks());
        if (!def.contains("DocSendMaxMsPerSlice")) def.setValue("DocSendMaxMsPerSlice", defaultDocSendMaxMsPerSlice());
        if (!def.contains("SocketReadSlackBytes")) def.setValue("SocketReadSlackBytes", defaultSocketReadSlackBytes());
        if (!def.contains("SocketWriteHighWaterBytes")) {
            def.setValue("SocketWriteHighWaterBytes",
                         QVariant::fromValue(defaultSocketWriteHighWaterBytes()));
        }
        def.endGroup();

        def.beginGroup("Avatar");
        if (!def.contains("MaxCacheEntries")) def.setValue("MaxCacheEntries", defaultAvatarCacheMaxCost());
        def.endGroup();

        def.beginGroup("AI");
        if (!def.contains("ApiUrl")) def.setValue("ApiUrl", defaultAiApiUrl());
        if (!def.contains("ApiKey")) def.setValue("ApiKey", defaultAiApiKey());
        if (!def.contains("Model")) def.setValue("Model", defaultAiModel());
        if (!def.contains("TimeoutMs")) def.setValue("TimeoutMs", defaultAiTimeoutMs());
        if (!def.contains("MaxMessages")) def.setValue("MaxMessages", defaultAiMaxMessages());
        if (!def.contains("MaxTranscriptChars")) {
            def.setValue("MaxTranscriptChars", defaultAiMaxTranscriptChars());
        }
        if (!def.contains("AnalyzeCooldownMs")) {
            def.setValue("AnalyzeCooldownMs", defaultAiAnalyzeCooldownMs());
        }
        def.endGroup();

        def.sync();
    });
}

// ===== AI =====
QString defaultAiApiUrl()
{
    return QStringLiteral("https://api.deepseek.com/chat/completions");
}
QString defaultAiApiKey() { return QStringLiteral("00000"); }
QString defaultAiModel() { return QStringLiteral("deepseek-chat"); }
int defaultAiTimeoutMs() { return 120000; }
int defaultAiMaxMessages() { return 2000; }
int defaultAiMaxTranscriptChars() { return 100000; }
int defaultAiAnalyzeCooldownMs() { return 5000; }

// 去除粘贴错误：BOM、首尾空白、重复的 Bearer、ini 行尾注释等。
static QString normalizeApiKeyInput(const QString &raw)
{
    QString k = raw.trimmed();
    while (!k.isEmpty() && (k.front() == QChar(0xFEFF) || k.front().isSpace())) {
        k.remove(0, 1);
    }
    k = k.trimmed();
    if (k.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive)) {
        k = k.mid(7).trimmed();
    }
    const int semi = k.indexOf(QLatin1Char(';'));
    if (semi >= 0) {
        k = k.left(semi).trimmed();
    }
    const int hash = k.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        k = k.left(hash).trimmed();
    }
    if (k.size() >= 2 && k.front() == QLatin1Char('"') && k.back() == QLatin1Char('"')) {
        k = k.mid(1, k.size() - 2).trimmed();
    }
    return k;
}

struct AiIniSnapshot {
    QString apiUrl;
    QString apiKey;
    QString model;
    int timeoutMs = 0;
    int maxMsg = 0;
    int maxChars = 0;
    int analyzeCooldownMs = 0;
};

static AiIniSnapshot loadAiIniSnapshot()
{
    ensureServerDefaults();
    AiIniSnapshot a;
    QSettings s(settingsIniPath(), QSettings::IniFormat);
    s.beginGroup("AI");
    a.apiUrl = s.value("ApiUrl", defaultAiApiUrl()).toString().trimmed();
    a.apiKey = normalizeApiKeyInput(s.value("ApiKey", defaultAiApiKey()).toString());
    a.model = s.value("Model", defaultAiModel()).toString().trimmed();
    if (a.model.isEmpty()) a.model = defaultAiModel();
    a.timeoutMs = s.value("TimeoutMs", defaultAiTimeoutMs()).toInt();
    if (a.timeoutMs < 5000 || a.timeoutMs > 600000) a.timeoutMs = defaultAiTimeoutMs();
    a.maxMsg = s.value("MaxMessages", defaultAiMaxMessages()).toInt();
    if (a.maxMsg < 1 || a.maxMsg > 10000) a.maxMsg = defaultAiMaxMessages();
    a.maxChars = s.value("MaxTranscriptChars", defaultAiMaxTranscriptChars()).toInt();
    if (a.maxChars < 4096 || a.maxChars > 500000) a.maxChars = defaultAiMaxTranscriptChars();
    a.analyzeCooldownMs = s.value("AnalyzeCooldownMs", defaultAiAnalyzeCooldownMs()).toInt();
    if (a.analyzeCooldownMs < 1000 || a.analyzeCooldownMs > 86400000) {
        a.analyzeCooldownMs = defaultAiAnalyzeCooldownMs();
    }
    s.endGroup();
    if (a.apiUrl.isEmpty()) a.apiUrl = defaultAiApiUrl();
    return a;
}

QString aiApiUrl() { return loadAiIniSnapshot().apiUrl; }
QString aiApiKey() { return loadAiIniSnapshot().apiKey; }
QString aiModel() { return loadAiIniSnapshot().model; }
int aiTimeoutMs() { return loadAiIniSnapshot().timeoutMs; }
int aiMaxMessages() { return loadAiIniSnapshot().maxMsg; }
int aiMaxTranscriptChars() { return loadAiIniSnapshot().maxChars; }
int aiAnalyzeCooldownMs() { return loadAiIniSnapshot().analyzeCooldownMs; }

}  // 命名空间

