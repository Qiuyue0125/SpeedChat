#ifndef LOGGER_H
#define LOGGER_H

/**
 * @file Logger.h
 * 日志初始化、级别过滤、文件轮转与 Qt 消息钩挂。
 */

#include <QString>
#include <QMutex>
#include <QFile>

// 增强日志管理：独立模块，支持级别过滤、文件轮转、控制台开关。
namespace Logger {

// 初始化日志（从 Settings.ini 的 [Log] 读取配置），并安装 Qt 消息处理器。
// 应在 main 中尽早调用。
void init();

// 启动日志不受 Level 限制结束后调用（进入 app->exec() 前），之后按 [Log]/Level 过滤。
void setStartupLoggingComplete();

// 关闭日志，释放文件句柄。
void shutdown();

// 强制刷新文件缓冲区。
void flush();

// 获取当前配置摘要（用于启动时打印）。
QString configSummary();

// 输出 PERF 指标（受 PerfMetricSampleEvery 控制：0=关闭，1=全量，N=每 N 次采样）。
void logPerfMetric(const char *name, qint64 value);

}  // namespace Logger

#endif // LOGGER_H
