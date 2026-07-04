/**
 * @file Logger.cpp
 * 服务端日志：级别、文件滚动、控制台输出与配置项。
 */
#include "Logger.h"
#include "ServerConfigDefaults.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QMutexLocker>
#include <atomic>

namespace Logger {

static QFile *s_logFile = nullptr;
static QMutex s_logMutex;
static QtMsgType s_minLogLevel = QtWarningMsg;
static bool s_flushEveryLine = false;
static bool s_consoleOutput = false;
static QString s_logLevelText = "warning";
static int s_maxFileSizeMB = 0;
static int s_fileCount = 10;
static qint64 s_currentFileSize = 0;
static int s_perfMetricSampleEvery = 0;  // 0=关闭，1=全量，N=每 N 次采样
static std::atomic<quint64> s_perfMetricCounter{0};
// true：启动阶段无视 [Log]/Level，仍输出信息级及以上（qInfo/qWarning/...），便于运维看到启动摘要。
static std::atomic<bool> s_startupBypassLevelFilter{true};

// 将 Qt 日志级别转为数值（用于比较）。
static int logSeverity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 0;
    case QtInfoMsg: return 1;
    case QtWarningMsg: return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg: return 4;
    default: return 1;
    }
}

// 解析日志级别字符串（debug/info/warning/error/fatal）。
static QtMsgType parseLogLevel(const QString &level)
{
    const QString lv = level.trimmed().toLower();
    if (lv == "debug") return QtDebugMsg;
    if (lv == "info") return QtInfoMsg;
    if (lv == "warning") return QtWarningMsg;
    if (lv == "error" || lv == "critical") return QtCriticalMsg;
    if (lv == "fatal") return QtFatalMsg;
    return QtWarningMsg;
}

// 日志等级
static QString logLevelName(QtMsgType level)
{
    switch (level) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "error";
    case QtFatalMsg: return "fatal";
    default: return "info";
    }
}

// 按大小轮转：若当前文件超过 maxSizeMB，重命名为 .1, .2 ... 并新建主文件。
// 调用方需已持有 s_logMutex。
static void maybeRotate(const QString &basePath, int maxSizeMB, int fileCount)
{
    if (maxSizeMB <= 0 || !s_logFile || !s_logFile->isOpen()) return;
    if (s_currentFileSize < static_cast<qint64>(maxSizeMB) * 1024 * 1024) return;

    s_logFile->close();
    delete s_logFile;
    s_logFile = nullptr;
    s_currentFileSize = 0;

    // 删除最老的备份
    const QString oldest = basePath + QString(".%1").arg(fileCount);
    if (QFile::exists(oldest)) QFile::remove(oldest);

    // 轮转：.3 -> .4, .2 -> .3, .1 -> .2, 主文件 -> .1
    for (int i = fileCount - 1; i >= 1; --i) {
        const QString src = (i == 1) ? basePath : (basePath + QString(".%1").arg(i - 1));
        const QString dst = basePath + QString(".%1").arg(i);
        if (QFile::exists(src)) QFile::rename(src, dst);
    }

    s_logFile = new QFile(basePath);
    if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        s_currentFileSize = s_logFile->size();
    } else {
        delete s_logFile;
        s_logFile = nullptr;
    }
}

// Qt 消息处理器：过滤级别、格式化输出、写入文件、控制台。
static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const bool startupBypass = s_startupBypassLevelFilter.load(std::memory_order_relaxed);
    const int msgSev = logSeverity(type);
    const int minSev = logSeverity(s_minLogLevel);
    // 运行期：低于配置级别则丢弃。启动期：仍放行信息级及以上（勿用 type>=QtInfoMsg：枚举值顺序与严重度不一致）。
    const bool pass = (msgSev >= minSev)
                      || (startupBypass && msgSev >= logSeverity(QtInfoMsg));
    if (!pass) {
        return;
    }

    const char *level = "调试";
    switch (type) {
    case QtInfoMsg:     level = "信息"; break;
    case QtWarningMsg: level = "警告"; break;
    case QtCriticalMsg: level = "严重"; break;
    case QtFatalMsg:    level = "致命"; break;
    default: break;
    }
    QString line = QDateTime::currentDateTime().toString(Qt::ISODate) + " [" + QString::fromUtf8(level) + "] " + msg + "\n";
    QByteArray utf8 = line.toUtf8();
    QByteArray local = line.toLocal8Bit();

    if (s_consoleOutput) {
        std::fprintf(stderr, "%s", local.constData());
        if (s_flushEveryLine || type >= QtWarningMsg || (startupBypass && msgSev >= logSeverity(QtInfoMsg))) {
            std::fflush(stderr);
        }
    }

    QMutexLocker lock(&s_logMutex);
    if (s_logFile && s_logFile->isOpen()) {
        s_logFile->write(utf8);
        s_currentFileSize += utf8.size();
        if (s_flushEveryLine || type >= QtWarningMsg || (startupBypass && msgSev >= logSeverity(QtInfoMsg))) {
            s_logFile->flush();
        }
        maybeRotate(s_logFile->fileName(), s_maxFileSizeMB, s_fileCount);
    }
}

// 初始化日志（从 Settings.ini 的 [Log] 读取配置），并安装 Qt 消息处理器
void init()
{
    ServerConfigDefaults::ensureServerDefaults();
    QSettings settings(ServerConfigDefaults::settingsIniPath(), QSettings::IniFormat);
    settings.beginGroup("Log");
    const QString level = settings.value("Level", ServerConfigDefaults::defaultLogLevel()).toString();
    s_minLogLevel = parseLogLevel(level);
    s_logLevelText = logLevelName(s_minLogLevel);
    s_flushEveryLine = settings.value("FlushEveryLine", ServerConfigDefaults::defaultLogFlushEveryLine()).toBool();
    s_consoleOutput = settings.value("ConsoleOutput", ServerConfigDefaults::defaultLogConsoleOutput()).toBool();
    s_maxFileSizeMB = settings.value("MaxFileSizeMB", ServerConfigDefaults::defaultLogMaxFileSizeMB()).toInt();
    s_fileCount = settings.value("FileCount", ServerConfigDefaults::defaultLogFileCount()).toInt();
    if (s_fileCount < 1) s_fileCount = 1;
    s_perfMetricSampleEvery = settings.value("PerfMetricSampleEvery", ServerConfigDefaults::defaultPerfMetricSampleEvery()).toInt();
    if (s_perfMetricSampleEvery < 0) s_perfMetricSampleEvery = 0;
    settings.endGroup();

    const QString logPath = ServerConfigDefaults::dataDir() + "server_log.txt";
    s_logFile = new QFile(logPath);
    if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        s_logFile->write(QString("\n--- %1 启动 ---\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
        s_logFile->flush();
        s_currentFileSize = s_logFile->size();
    } else {
        delete s_logFile;
        s_logFile = nullptr;
    }

    qInstallMessageHandler(messageHandler);
}

// 启动阶段结束后恢复严格按 [Log]/Level 过滤（应在进入事件循环之前调用一次）。
void setStartupLoggingComplete()
{
    s_startupBypassLevelFilter.store(false, std::memory_order_relaxed);
}

// 关闭日志，释放文件句柄。
void shutdown()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker lock(&s_logMutex);
    if (s_logFile) {
        s_logFile->close();
        delete s_logFile;
        s_logFile = nullptr;
    }
}

// 强制刷新文件缓冲区。
void flush()
{
    QMutexLocker lock(&s_logMutex);
    if (s_logFile && s_logFile->isOpen()) {
        s_logFile->flush();
    }
}

// 获取当前配置摘要（用于启动时打印）。
QString configSummary()
{
    QStringList parts;
    parts << "Level=" + s_logLevelText;
    parts << "FlushEveryLine=" + QString(s_flushEveryLine ? "true" : "false");
    parts << "ConsoleOutput=" + QString(s_consoleOutput ? "true" : "false");
    if (s_maxFileSizeMB > 0) {
        parts << "MaxFileSizeMB=" + QString::number(s_maxFileSizeMB);
        parts << "FileCount=" + QString::number(s_fileCount);
    } else {
        parts << "Rotation=off";
    }
    if (s_perfMetricSampleEvery == 0) {
        parts << "PerfMetric=off";
    } else if (s_perfMetricSampleEvery == 1) {
        parts << "PerfMetric=full";
    } else {
        parts << "PerfMetric=sample/" + QString::number(s_perfMetricSampleEvery);
    }
    return parts.join(" ");
}

// 输出 PERF 指标（受 PerfMetricSampleEvery 控制：0=关闭，1=全量，N=每 N 次采样）。
void logPerfMetric(const char *name, qint64 value)
{
    if (s_perfMetricSampleEvery == 0) return;
    if (s_perfMetricSampleEvery > 1) {
        const quint64 v = s_perfMetricCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((v % static_cast<quint64>(s_perfMetricSampleEvery)) != 0) return;
    }
    qInfo().noquote() << QString("PERF %1=%2").arg(name).arg(value);
}

}  // namespace Logger
