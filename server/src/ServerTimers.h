#ifndef SERVERTIMERS_H
#define SERVERTIMERS_H

/**
 * @file ServerTimers.h
 * 空闲检测、文档泵送、上传会话清理等定时器。
 */

#include <QObject>
#include <QTimer>

class ServerCore;

// 统一计时器：空闲检查、文档发送泵送、上传清理。
class ServerTimers : public QObject
{
    Q_OBJECT
public:
    // 构造定时器，core 为 ServerCore 指针
    explicit ServerTimers(ServerCore *core, QObject *parent = nullptr);
    // 析构时停止所有定时器
    ~ServerTimers();

    // 启动所有定时器（空闲检查、文档发送泵送、上传清理）。
    void start();
    // 停止所有定时器。
    void stop();
    // 按需启停文档发送泵送定时器（无 pending 时可关闭 10ms tick）。
    void setDocSendEnabled(bool enabled);

private slots:
    // 遍历所有连接，调用 checkIdleTimeout（使用 Server 维护的列表）。
    void onIdleCheckTick();
    // 仅对有 pending 数据的连接调用 pumpDocSend。
    void onDocSendTick();
    // 仅对有活跃上传的连接调用 checkUploadCleanup。
    void onUploadCleanupTick();

private:
    ServerCore *m_core = nullptr;
    QTimer *m_idleCheckTimer = nullptr;
    QTimer *m_docSendTimer = nullptr;
    QTimer *m_uploadCleanupTimer = nullptr;
};

#endif // SERVERTIMERS_H
