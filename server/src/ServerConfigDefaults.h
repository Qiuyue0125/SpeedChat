#pragma once

#include <QString>
#include <QStringList>
#include <QSet>

/**
 * @file ServerConfigDefaults.h
 */

/**
 * ServerConfigDefaults - 服务端默认配置集中管理
 *
 * 配置文件路径：程序目录/Settings.ini（由 settingsIniPath() 返回）
 * 首次调用 ensureServerDefaults() 或读取配置时，会自动补齐缺失键并写入默认值。
 *
 * ===== 代码默认值（default* 函数）=====
 *
 * 【Server 基础】
 *   defaultListenPort(20001)        - 监听端口
 *   defaultMaxDbConnections(128)    - 数据库连接池最大连接数
 *   defaultThreadPoolMaxThreads(120) - 业务线程池最大线程数（[ThreadPool]/MaxThreads 缺省）
 *   defaultClientIdleTimeoutMs(60000) - 客户端空闲超时（毫秒），超时断开
 *   defaultStopCheckIntervalMs(5000)  - 停止检测间隔（毫秒）
 *   defaultBackpressureDroppableTags("heart,pong") - 背压时可丢弃的 tag
 *   defaultRateLimitWindowMs(1000)  - 限流窗口长度（毫秒）
 *   defaultHeartMaxPerWindow(100)  - 每窗口 heart 最大次数（与 Qt 客户端默认约 30s/ 次 heart 搭配足够）
 *   defaultUnknownTagMaxPerWindow(50) - 每窗口未知 tag 最大次数
 *   defaultDisconnectOnUnknownTagLimit(true) - 未知 tag 超限是否断开
 *
 * 【ServerTimers】
 *   defaultIdleCheckIntervalMs(5000)     - 空闲检查定时器间隔
 *   defaultDocSendIntervalMs(10)         - 文档发送泵送定时器间隔
 *   defaultUploadCleanupIntervalMs(30000) - 上传会话清理定时器间隔
 *
 * 【数据库】
 *   defaultDbHost/DbPort/DbName/DbUser/DbPassword - MySQL 连接参数
 *
 * 【DatabasePool】
 *   defaultDatabasePoolConnConnectTimeoutSec(15) - 建立连接超时（秒）
 *
 * 【线程池】
 *   defaultThreadPoolMaxThreads/IdleTimeoutMs/CheckIntervalMs
 *
 * 【ClientHandler 传输】
 *   defaultDocChunkBytes(64KB) / defaultUploadChunkBytes(64KB)
 *   defaultDocRateLimitEnabled(true) — 默认启用下载令牌桶限速
 *   defaultDocRateBytesPerSec(8MB/s) / defaultDocBurstBytes(4MB)
 *   defaultDocSendQueueMaxPackets(65536) / defaultDocSendQueueMaxBytes(1610612736, ~1.5GiB)
 *   defaultMaxConcurrentUploadsPerConn(3) / defaultUploadIdleTimeoutMs(60s)
 *
 * 【ClientHandler 队列与时间片】
 *   defaultReadyReadMaxPacketsPerSlice(64) / defaultReadyReadMaxMsPerSlice(2)
 *   defaultDocSendMaxMsPerSlice(2) / defaultDocSendPostBatchChunks(8) / defaultSocketReadSlackBytes(128)
 *   defaultUploadWriteBatchMaxChunks(8)
 *   defaultJsonParseQueueMaxPackets(1024) / defaultJsonParseQueueMaxBytes(16MB)
 *   defaultBinaryParseQueueMaxPackets(2048) / defaultBinaryParseQueueMaxBytes(256MB)
 *   defaultOutQueueMaxPackets(65536) / defaultOutQueueMaxBytes(1610612736, ~1.5GiB)
 *   defaultSocketWriteHighWaterBytes(16MB，可由 ini [ClientHandler]/SocketWriteHighWaterBytes 覆盖)
 *
 * 【ClientHandlerShared】
 *   defaultDbDispatchRetryIntervalMs(2) / defaultDbDispatchRetryMaxIntervalMs(64)
 *   defaultDbDispatchMaxWaitMs(20000) - 线程池调度超时则丢弃任务
 *   defaultShouldSampleEvery(128) / defaultAvatarCacheMaxCost(0) — 头像 LRU 条数，0=禁用
 *
 * 【日志】
 *   defaultLogLevel("warning") / defaultLogFlushEveryLine(false)
 *   defaultLogConsoleOutput(false) / defaultLogMaxFileSizeMB(50) / defaultLogFileCount(10)
 *   defaultPerfMetricSampleEvery(0) / defaultNoisyLogSampleEvery(0)
 *
 * ===== Settings.ini 配置节与键 =====
 *
 * [Server]
 *   ListenPort, ClientIdleTimeoutMs, StopCheckIntervalMs
 *   BackpressureDroppableTags, RateLimitWindowMs, HeartMaxPerWindow
 *   UnknownTagMaxPerWindow, DisconnectOnUnknownTagLimit
 *
 * [Database]
 *   Host, Port, Database, User, Password
 *
 * [DatabasePool]
 *   MaxConnections, ConnConnectTimeoutSec, AllowSetGlobalMaxConnections
 *
 * [ThreadPool]
 *   MaxThreads, IdleTimeoutMs, CheckIntervalMs
 *   （已移除 WarmupThreads / 预热，启动为冷启动按需建线程）
 *
 * [Log]
 *   Level, FlushEveryLine, ConsoleOutput, MaxFileSizeMB, FileCount
 *   PerfMetricSampleEvery, NoisyLogSampleEvery
 *
 * [ClientHandler]
 *   DocChunkBytes, UploadChunkBytes — 文档下载读盘分片 / 上传会话分片（字节）
 *   RateLimitEnabled — 是否启用下载限速（true=启用，默认）
 *   DocRateBytesPerSec, DocBurstBytes — 文档下发令牌桶速率与突发
 *   DocSendPostBatchChunks — 文档下载线程每批投递主线程的二进制帧数
 *   DocSendMaxMsPerSlice — pumpDocSend 单次最大耗时（毫秒）
 *   SocketReadSlackBytes — QTcpSocket 读缓冲上限 = max(JSON,Binary) 单帧 + 本值
 *   SocketWriteHighWaterBytes — pumpSocketWrite 时若 bytesToWrite 超过本值则暂停从 m_outQueue 取包（背压）
 *   DocSendQueueMaxPackets/Bytes, JsonParseQueueMaxPackets/Bytes
 *   OutQueueMaxPackets/Bytes, BinaryParseQueueMaxPackets/Bytes, DbDispatchMaxWaitMs
 *
 * [Avatar]
 *   MaxCacheEntries — 全局头像 Base64 LRU 最大条数；0=不缓存（每次读盘）
 *
 * [AI]（聊天 AI 分析，每次请求重新读取 ini 便于热更新）
 *   ApiUrl — 默认 https://api.deepseek.com/chat/completions
 *   ApiKey — 默认 00000
 *   Model — 默认 deepseek-chat
 *   TimeoutMs — HTTP 超时（毫秒），默认 120000
 *   MaxMessages — 单次分析最多拉取消息条数，默认 2000
 *   MaxTranscriptChars — 拼进提示的聊天记录最大字符数，默认 100000
 *   AnalyzeCooldownMs — 同一账号两次发起 AI 分析的最小间隔（毫秒），默认 5000
 *
 * 运行时读取：serverListenPort(), serverClientIdleTimeoutMs(), serverBackpressureDroppableTagSet()
 * threadPoolMaxThreads(), databasePoolMaxConnections(), serverDocSendQueueMaxPackets() 等
 */

namespace ServerConfigDefaults {

// 获取数据根目录（AppImage 环境 → $HOME/.suliao-server/，否则 applicationDirPath/）。
QString dataDir();
// 获取配置文件路径（基于 dataDir()）。
QString settingsIniPath();
// 获取存储根目录（基于 dataDir() + storage/）。
QString storageBasePath();
// 写入缺失默认配置。
void ensureServerDefaults();

// ===== Server 基础 =====
// 默认监听端口。
quint16 defaultListenPort();
// 默认数据库连接池最大连接数。
int defaultMaxDbConnections();
// 默认客户端空闲超时（毫秒）。
int defaultClientIdleTimeoutMs();
// 默认停止检测间隔（毫秒）。
int defaultStopCheckIntervalMs();
// 默认背压时可丢弃的 tag 列表（逗号分隔）。
QString defaultBackpressureDroppableTags();
// 默认限流窗口长度（毫秒）。
int defaultRateLimitWindowMs();
// 默认每窗口允许的 heart 最大次数（超出将丢弃）。
int defaultHeartMaxPerWindow();
// 默认每窗口允许的未知 tag 最大次数（超出将丢弃或断开）。
int defaultUnknownTagMaxPerWindow();
// 默认未知 tag 超限时是否断开连接。
bool defaultDisconnectOnUnknownTagLimit();

// 从 Settings.ini 读取监听端口（1-65535，带兜底默认值）。
quint16 serverListenPort();
// 从 Settings.ini 读取客户端空闲超时（毫秒，>0，带兜底默认值）。
int serverClientIdleTimeoutMs();

// 从 Settings.ini 读取背压时可丢弃的 tag 集合（带兜底默认值，适合热路径 contains）。
// 返回引用指向进程内缓存，避免频繁分配。
const QSet<QString> &serverBackpressureDroppableTagSet();
// 从 Settings.ini 读取限流窗口长度（毫秒，带兜底默认值）。
int serverRateLimitWindowMs();
// 从 Settings.ini 读取每窗口允许的 heart 最大次数（带兜底默认值）。
int serverHeartMaxPerWindow();
// 从 Settings.ini 读取每窗口允许的未知 tag 最大次数（带兜底默认值）。
int serverUnknownTagMaxPerWindow();
// 从 Settings.ini 读取未知 tag 超限时是否断开连接（带兜底默认值）。
bool serverDisconnectOnUnknownTagLimit();

// 从 Settings.ini 读取业务线程池最大线程数（带兜底默认值）。
// 读取 [ThreadPool]/MaxThreads。
int threadPoolMaxThreads();
// 从 Settings.ini 读取线程空闲超时（毫秒，带兜底默认值）。
// 读取 [ThreadPool]/IdleTimeoutMs。
int threadPoolIdleTimeoutMs();
// 从 Settings.ini 读取线程空闲检查间隔（毫秒，带兜底默认值）。
// 读取 [ThreadPool]/CheckIntervalMs。
int threadPoolCheckIntervalMs();
// 从 Settings.ini 读取数据库连接池最大连接数（带兜底默认值）。
// 读取 [DatabasePool]/MaxConnections。
int databasePoolMaxConnections();

// ===== ServerTimers =====
// 默认空闲检查定时器间隔（毫秒）。
int defaultIdleCheckIntervalMs();
// 默认文档发送泵送定时器间隔（毫秒）。
int defaultDocSendIntervalMs();
// 默认上传会话清理定时器间隔（毫秒）。
int defaultUploadCleanupIntervalMs();

// ===== 数据库 =====
// 默认数据库主机地址。
QString defaultDbHost();
// 默认数据库端口。
int defaultDbPort();
// 默认数据库名称。
QString defaultDbName();
// 默认数据库用户名。
QString defaultDbUser();
// 默认数据库密码。
QString defaultDbPassword();
// 默认数据库连接建立超时（秒），与 [DatabasePool]/ConnConnectTimeoutSec 一致。
int defaultDatabasePoolConnConnectTimeoutSec();

// ===== 线程池 =====
// 默认线程池最大线程数。
int defaultThreadPoolMaxThreads();
// 默认线程空闲超时（毫秒）。
int defaultThreadPoolIdleTimeoutMs();
// 默认线程空闲检查间隔（毫秒）。
int defaultThreadPoolCheckIntervalMs();

// ===== ClientHandler 传输 =====
// 默认文档下载分片大小（字节）。
int defaultDocChunkBytes();
// 默认上传分片大小（字节）。
int defaultUploadChunkBytes();
// 默认是否启用下载限速（true=启用）。
bool defaultDocRateLimitEnabled();
// 默认文档下发速率限制（字节/秒）。
int defaultDocRateBytesPerSec();
// 默认文档下发突发额度（字节）。
int defaultDocBurstBytes();
// 默认文档发送队列包数上限。
int defaultDocSendQueueMaxPackets();
// 默认文档发送队列字节上限。
int defaultDocSendQueueMaxBytes();
// 默认单连接最大并发上传数。
int defaultMaxConcurrentUploadsPerConn();
// 默认上传会话空闲超时（毫秒）。
qint64 defaultUploadIdleTimeoutMs();

// ===== ClientHandler 队列与时间片 =====
// 默认 readyRead 单次处理最大包数。
int defaultReadyReadMaxPacketsPerSlice();
// 默认 readyRead 单次处理最大耗时（毫秒）。
qint64 defaultReadyReadMaxMsPerSlice();
// 默认文档发送单次时间片（毫秒）。
int defaultDocSendMaxMsPerSlice();
// 默认上传写盘单批最大分片数。
int defaultUploadWriteBatchMaxChunks();
// 默认上传写盘单批最大字节数。
int defaultUploadWriteBatchMaxBytes();
// 默认转发单批最大条目数。
int defaultForwardBatchItemsPerSlice();
// 默认 JSON 解析队列包数上限。
int defaultJsonParseQueueMaxPackets();
// 默认 JSON 解析队列字节上限。
int defaultJsonParseQueueMaxBytes();
// 默认单次 JSON 解析批处理最大包数。
int defaultJsonParseBatchMaxPackets();
// 默认单次 JSON 解析批处理最大字节数。
int defaultJsonParseBatchMaxBytes();
// 默认二进制解析队列包数上限。
int defaultBinaryParseQueueMaxPackets();
// 默认二进制解析队列字节上限。
int defaultBinaryParseQueueMaxBytes();
// 默认原始发送队列包数上限。
int defaultOutQueueMaxPackets();
// 默认原始发送队列字节上限。
int defaultOutQueueMaxBytes();
// 默认 socket 写缓冲高水位（字节）；可由 [ClientHandler]/SocketWriteHighWaterBytes 覆盖。
qint64 defaultSocketWriteHighWaterBytes();
// 默认文档下载线程向主线程每批投递的二进制帧数。
int defaultDocSendPostBatchChunks();
// 默认 QTcpSocket 读缓冲余量（字节）：读缓冲上限 = 单帧最大负载 + 本值。
qint64 defaultSocketReadSlackBytes();

// ===== ClientHandlerShared =====
// 默认线程池调度重试初始间隔（毫秒）。
int defaultDbDispatchRetryIntervalMs();
// 默认线程池调度重试间隔上限（毫秒）。
int defaultDbDispatchRetryMaxIntervalMs();
// 默认线程池调度最大等待时间（毫秒）。
int defaultDbDispatchMaxWaitMs();
// 从 Settings.ini [ClientHandler] 读取，压测时可调大减少任务丢弃
int dbDispatchMaxWaitMs();
// 默认日志采样周期（每 N 次输出一次）。
quint64 defaultShouldSampleEvery();
// 代码默认头像缓存最大条目数（写入 ini 缺省键；0=禁用缓存）。
int defaultAvatarCacheMaxCost();
// 从 Settings.ini [Avatar]/MaxCacheEntries 读取（0=禁用）。
int avatarCacheMaxEntries();

// ===== AI（聊天分析，每次请求从 Settings.ini 读取，便于热更新）=====
// [AI] ApiUrl / ApiKey / Model / TimeoutMs / MaxMessages / MaxTranscriptChars / AnalyzeCooldownMs
QString defaultAiApiUrl();
QString defaultAiApiKey();
QString defaultAiModel();
int defaultAiTimeoutMs();
int defaultAiMaxMessages();
int defaultAiMaxTranscriptChars();
int defaultAiAnalyzeCooldownMs();
QString aiApiUrl();
QString aiApiKey();
QString aiModel();
int aiTimeoutMs();
int aiMaxMessages();
int aiMaxTranscriptChars();
int aiAnalyzeCooldownMs();

// ===== 日志 =====
// 默认日志级别（debug/info/warning/error/critical/fatal）。
QString defaultLogLevel();
// 默认是否每行刷盘。
bool defaultLogFlushEveryLine();
// 默认是否输出到控制台。
bool defaultLogConsoleOutput();
// 默认单日志文件最大大小（MB），0 表示不轮转。
int defaultLogMaxFileSizeMB();
// 默认轮转保留文件数量。
int defaultLogFileCount();
// 默认 PERF 指标采样（0=关闭，1=全量，N=每 N 次采样）。
int defaultPerfMetricSampleEvery();
// 默认噪声/高频日志采样（0=关闭，1=全量，N=每 N 次采样）。
int defaultNoisyLogSampleEvery();
// 从 Settings.ini 读取噪声/高频日志采样（0=关闭，1=全量，N=每 N 次采样）。
int logNoisyLogSampleEvery();

// 从 Settings.ini 读取是否允许修改 MySQL 全局 max_connections（默认 false）。
bool databasePoolAllowSetGlobalMaxConnections();

// ===== ClientHandler 队列限制（从 Settings.ini 读取） =====
int serverDocSendQueueMaxPackets();
int serverDocSendQueueMaxBytes();
int serverJsonParseQueueMaxPackets();
int serverJsonParseQueueMaxBytes();
int serverOutQueueMaxPackets();
int serverOutQueueMaxBytes();
int serverBinaryParseQueueMaxPackets();
int serverBinaryParseQueueMaxBytes();
// 以下 [ClientHandler] 传输与时间片（与 default* 同源读取 ini）
int serverDocChunkBytes();
int serverUploadChunkBytes();
bool serverDocRateLimitEnabled();
int serverDocRateBytesPerSec();
int serverDocBurstBytes();
int serverDocSendPostBatchChunks();
int serverDocSendMaxMsPerSlice();
qint64 serverSocketReadSlackBytes();
// 从 Settings.ini [ClientHandler]/SocketWriteHighWaterBytes 读取（pumpSocketWrite 背压）。
qint64 serverSocketWriteHighWaterBytes();

}  // namespace ServerConfigDefaults

