/**
 * @file ClientConfigDefaults.cpp
 * ClientConfigDefaults 实现，详见 ClientConfigDefaults.h。
 */
#include "ClientConfigDefaults.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace ClientConfigDefaults {

// 获取数据根目录（AppImage 环境 → $HOME/.suliao/，否则 applicationDirPath/）。
QString dataDir()
{
    const QString appDirEnv = qEnvironmentVariable("APPDIR");
    if (!appDirEnv.isEmpty()) {
        return QDir::homePath() + QDir::separator() + QLatin1String(".suliao") + QDir::separator();
    }
    return QCoreApplication::applicationDirPath() + QDir::separator();
}

// 获取配置文件路径（基于 dataDir()）。
QString settingsIniPath()
{
    return dataDir() + QLatin1String("Settings.ini");
}

// 写入缺失默认配置
void ensureClientDefaults()
{
    const QString path = settingsIniPath();
    QDir().mkpath(dataDir());
    QSettings def(path, QSettings::IniFormat);

    def.beginGroup("Socket");
    if (!def.contains("HostName")) def.setValue("HostName", defaultSocketHostName());
    if (!def.contains("Port")) def.setValue("Port", defaultSocketPort());
    def.endGroup();

    def.beginGroup("Login");
    if (!def.contains("autologin")) def.setValue("autologin", false);
    def.endGroup();

    // 写入助手默认配置
    def.beginGroup("AI");
    if (!def.contains("Providers")) {
        def.setValue("Providers", defaultAiProvidersJson());
    }
    if (!def.contains("SelectedIndex")) def.setValue("SelectedIndex", defaultAiSelectedIndex());
    def.endGroup();

    // 写入上传限速默认配置
    def.beginGroup("Upload");
    if (!def.contains("RateLimitEnabled")) def.setValue("RateLimitEnabled", defaultUploadRateLimitEnabled());
    if (!def.contains("UploadRateBytesPerSec")) def.setValue("UploadRateBytesPerSec", defaultUploadRateBytesPerSec());
    if (!def.contains("UploadBurstBytes")) def.setValue("UploadBurstBytes", defaultUploadBurstBytes());
    def.endGroup();

    def.sync();
}

// 默认地址
QString defaultSocketHostName() { return "127.0.0.1"; }
// 默认端口
quint16 defaultSocketPort() { return 20001; }
// 默认应用层心跳间隔（毫秒）
int defaultSocketHeartbeatIntervalMs() { return 30 * 1000; }
// 断线后最大自动重连次数（达到后停止重连并上抛失败信号）
int defaultSocketMaxReconnectAttempts() { return 5; }
// 乐观发送后等待服务端回执的最长时间（须大于常网络 RTT，避免误杀）
int defaultOutboundMessageAckTimeoutMs() { return 25000; }
// 扫描 m_pendingMessageAckDeadlineMs 的定时器周期
int defaultPendingMessageAckSweepIntervalMs() { return 500; }
// 默认助手列表
QString defaultAiProvidersJson()
{
    return QString::fromUtf8(
        R"([{"name":"DeepSeek","url":"https://chat.deepseek.com"},{"name":"Kimi","url":"https://kimi.moonshot.cn"},{"name":"豆包","url":"https://www.doubao.com"},{"name":"元宝","url":"https://yuanbao.tencent.com"},{"name":"文心","url":"https://yiyan.baidu.com"},{"name":"通义","url":"https://www.qianwen.com"}])");
}
// 默认助手选择
int defaultAiSelectedIndex() { return 0; }
// 默认启用上传限速
bool defaultUploadRateLimitEnabled() { return true; }
// 默认上传速率（与 Settings.ini [Upload]/UploadRateBytesPerSec 一致，16 MiB/s）
int defaultUploadRateBytesPerSec() { return 16777216; }
// 默认上传突发额度（与 Settings.ini [Upload]/UploadBurstBytes 一致，16 MiB）
int defaultUploadBurstBytes() { return 16777216; }

}  // 命名空间