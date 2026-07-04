/**
 * @file main.cpp
 * 服务端入口：日志与崩溃钩、加载 Settings.ini、启动 Server 或 ServerCore。
 */
#include "Logger.h"
#include "ServerCore.h"
#include "ServerConfigDefaults.h"
#include "ThreadPool.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QDateTime>
#include <QTextStream>

#include <csignal>
#include <cstdlib>
#include <memory>

// ===== 跨平台崩溃信号处理 =====
static QString s_crashLogPath;

static void crashSignalHandler(int sig)
{
    const QString path = s_crashLogPath.isEmpty() ? (QDir::currentPath() + "/server_crash.txt") : s_crashLogPath;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out << "Server crashed at " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Signal: " << sig << " (";

        switch (sig) {
        case SIGSEGV: out << "SIGSEGV - Segmentation Fault"; break;
        case SIGABRT: out << "SIGABRT - Aborted"; break;
        case SIGFPE:  out << "SIGFPE - Floating Point Exception"; break;
        case SIGILL:  out << "SIGILL - Illegal Instruction"; break;
#ifdef SIGBUS
        case SIGBUS:  out << "SIGBUS - Bus Error"; break;
#endif
#ifdef SIGPIPE
        case SIGPIPE: out << "SIGPIPE - Broken Pipe"; break;
#endif
        default:      out << "Unknown"; break;
        }
        out << ")\n";
        out << "Check server_log.txt for detailed logs before crash.\n";
        f.close();
    }

    // 恢复默认行为并重新抛出，让系统生成 core dump（如果启用）
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

static void installCrashHandlers()
{
    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGFPE,  crashSignalHandler);
    std::signal(SIGILL,  crashSignalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS,  crashSignalHandler);
#endif
}


namespace {

// headless 模式下的停止轮询间隔（通过创建 stop.txt 触发退出）。
int loadStopCheckIntervalMs(const QString &configPath)
{
    if (!QFile::exists(configPath)) {
        return ServerConfigDefaults::defaultStopCheckIntervalMs();
    }
    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup("Server");
    int ms = settings.value("StopCheckIntervalMs", ServerConfigDefaults::defaultStopCheckIntervalMs()).toInt();
    settings.endGroup();
    return ms > 0 ? ms : ServerConfigDefaults::defaultStopCheckIntervalMs();
}

} // namespace

// 程序入口：纯命令行服务端，初始化日志并启动 ServerCore。
int main(int argc, char *argv[])
{
    if (argc > 0 && argv[0]) {
        s_crashLogPath = ServerConfigDefaults::dataDir() + "server_crash.txt";
    }
    installCrashHandlers();

    const QString configPath = ServerConfigDefaults::settingsIniPath();

    QCoreApplication app(argc, argv);

    // 统一安装 Qt message handler（写文件/控制台、轮转等）。
    Logger::init();
    qInfo().nospace() << "日志配置：" << Logger::configSummary();

    ServerConfigDefaults::ensureServerDefaults();

    ServerCore core;
    if (!core.initialize()) {
        qCritical("ServerCore 初始化失败，退出");
        return 1;
    }
    qInfo().nospace() << "无界面模式：端口 " << core.listenPort() << "，日志见 server_log.txt";

    if (!core.startListening()) {
        qCritical("监听失败，退出");
        return 1;
    }

    const int stopCheckMs = loadStopCheckIntervalMs(configPath);
    QTimer *stopCheck = new QTimer(&app);
    QObject::connect(stopCheck, &QTimer::timeout, &app, [stopCheck]() {
        const QString path = ServerConfigDefaults::dataDir() + "stop.txt";
        if (QFile::exists(path)) {
            QFile::remove(path);
            qInfo() << "【服务器】检测到 stop.txt，正在退出";
            QCoreApplication::quit();
        }
    });
    stopCheck->start(stopCheckMs);

    // 在事件循环关闭之前（aboutToQuit）主动停止定时器、清理线程池，避免跨线程 timer 警告。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [stopCheck]() {
        if (stopCheck) stopCheck->stop();
        ThreadPool::getInstance().clear();
    });

    Logger::setStartupLoggingComplete();
    const int ret = app.exec();

    qInfo() << "【服务器】正在关闭";
    Logger::shutdown();
    return ret;
}