#pragma once

#include <QString>

/**
 * @file ClientConfigDefaults.h
 */

/**
 * ClientConfigDefaults - 客户端默认配置集中管理
 *
 * 配置文件路径：程序目录/Settings.ini（由 settingsIniPath() 返回）
 * 首次调用 ensureClientDefaults() 时，会自动补齐缺失键并写入默认值。
 *
 * ===== 代码默认值（default* 函数）=====
 *
 * 【连接】
 *   defaultSocketHostName("127.0.0.1") - 服务器地址
 *   defaultSocketPort(20001)            - 服务器端口
 *   defaultSocketHeartbeatIntervalMs(30000) - 应用层 heart 发送间隔（仅代码常量，不写 Settings.ini）
 *   defaultSocketMaxReconnectAttempts(5) - 断线后最大自动重连次数（仅代码常量，不写 Settings.ini）
 *   defaultOutboundMessageAckTimeoutMs(25000) - 发出消息后多久内未收到服务端 messagehavedone 则视为未送达（仅代码常量）
 *   defaultPendingMessageAckSweepIntervalMs(500) - 扫描上述超时任务的定时器周期（仅代码常量）
 *
 * 【AI 助手】
 *   defaultAiProvidersJson() - 默认助手列表（DeepSeek/Kimi/豆包/元宝/文心/通义等）
 *   defaultAiSelectedIndex(0) - 默认选中的助手索引
 *
 * 【上传限速】
 *   defaultUploadRateLimitEnabled(true)     - 默认启用令牌桶限速
 *   defaultUploadRateBytesPerSec(16777216) - 上传速率限制（字节/秒，16 MiB/s）
 *   defaultUploadBurstBytes(16777216)      - 上传突发额度（16 MiB）
 *   ensureClientDefaults 会写入 [Upload]/RateLimitEnabled、UploadRateBytesPerSec、UploadBurstBytes（缺省时）
 *
 * ===== Settings.ini 配置节与键 =====
 *
 * [Socket]
 *   HostName - 服务器地址（IP 或域名）
 *   Port     - 服务器端口（1-65535）
 *
 * [Login]
 *   autologin - 是否自动登录（true/false）
 *
 * [AI]
 *   Providers     - 助手列表，JSON 数组，每项 {"name":"名称","url":"https://..."}
 *   SelectedIndex - 当前选中的助手索引（0 起）
 *
 * [Upload]
 *   RateLimitEnabled      - 是否启用上传限速（true=启用，默认）
 *   UploadRateBytesPerSec - 上传令牌桶填充速率（字节/秒）
 *   UploadBurstBytes      - 上传突发桶容量（字节）
 */

namespace ClientConfigDefaults {

// 获取数据根目录（AppImage 环境 → $HOME/.suliao/，否则 applicationDirPath/）。
QString dataDir();
// 获取配置文件路径（基于 dataDir()）。
QString settingsIniPath();
// 写入缺失默认配置
void ensureClientDefaults();
// 默认地址
QString defaultSocketHostName();
// 默认端口
quint16 defaultSocketPort();
// 默认应用层心跳间隔（毫秒）
int defaultSocketHeartbeatIntervalMs();
// 默认最大自动重连次数（仅代码常量，不写配置文件）
int defaultSocketMaxReconnectAttempts();
// 乐观发送后等待 messagehavedone（uuid）的最长时间（毫秒），超时按未送达处理
int defaultOutboundMessageAckTimeoutMs();
// 待发消息 ack 超时扫描定时器间隔（毫秒 MainWindow 内 QTimer）
int defaultPendingMessageAckSweepIntervalMs();
// 默认助手列表
QString defaultAiProvidersJson();
// 默认助手选择
int defaultAiSelectedIndex();

// 默认是否启用上传限速（true=启用）
bool defaultUploadRateLimitEnabled();
// 默认上传速率
int defaultUploadRateBytesPerSec();
// 默认上传突发额度
int defaultUploadBurstBytes();

}  // namespace ClientConfigDefaults

