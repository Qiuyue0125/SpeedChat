#ifndef SOCKETDOC_H
#define SOCKETDOC_H

/**
 * @file SocketDoc.h
 * 独立线程大文件发送：分片、令牌桶限流与协议封装。
 */

#include <QObject>
#include <QThread>
#include <QTcpSocket>
#include <QScopedPointer>
#include <QTimer>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <cstdint>
#include "SocketDepend.h"

// 文件发送套接字处理类
// 单例模式独立线程处理文件发送通信 支持协议头封装心跳检测断线重连
class SocketDocWrite : public QObject
{
    Q_OBJECT
    template <typename T>
    friend struct QScopedPointerDeleter;

public:
    // 构造函数
    explicit SocketDocWrite(QObject* parent = nullptr);

    // 析构函数
    ~SocketDocWrite() override;

    // 禁用拷贝构造和赋值运算符 单例模式保护
    SocketDocWrite(const SocketDocWrite&) = delete;
    SocketDocWrite& operator=(const SocketDocWrite&) = delete;

    // 获取单例实例
    static SocketDocWrite& instance();

    // 线程安全发送数据
    void sendData(const QByteArray& data, const QString& filename = "");
    // 发送文件
    void sendFile(const QString &filePath,
                  const QString &sender,
                  const QString &receiver,
                  const QString &filename,
                  const QString &timestamp,
                  const QString &uuid);
    // 上传文件到网盘（无需指定接收者）
    void sendCloudFile(const QString &filePath,
                       const QString &sender,
                       const QString &filename,
                       const QString &timestamp,
                       const QString &uuid);

    // 手动释放单例资源 释放后需要重新获取实例
    Q_INVOKABLE static void releaseInstance();

signals:
    // 套接字连接成功信号
    void socketConnected();

    // 套接字断开连接信号
    void socketDisconnected();

    // 套接字错误信号
    void socketError(const QString& errorString);

    // 文件发送完成信号
    void fileUpdate(const QString& fileName, const QString& uuid);
    // 网盘文件上传完成（返回 file_id）
    void cloudFileUploaded(const QString &fileId, const QString &filename, qint64 fileSize);
    // 上传进度信号（receivedBytes 来自 upload_ack，表示服务端已落盘字节数）
    void uploadProgress(const QString &uuid, qint64 receivedBytes, qint64 totalBytes);

private:
    // 重置上传会话状态，必要时断开socket强制下次重连
    void resetUploadSession(bool closeSocket);

    // 启动发送文件任务
    Q_INVOKABLE void startSendFile(const QString &filePath,
                                  const QString &sender,
                                  const QString &receiver,
                                  const QString &filename,
                                  const QString &timestamp,
                                  const QString &uuid,
                                  bool isCloud = false);

    // 发送下一片文件数据
    void sendNextChunk();

    // 执行套接字操作 跨线程调用接口
    Q_INVOKABLE void executeSocketOp(const QByteArray& data, const QString& filename, bool isPending);

    // 清理套接字相关资源 关闭连接释放对象并重置状态
    Q_INVOKABLE void cleanupSocketResources();

    // 处理服务器返回的文件完成标识
    Q_INVOKABLE void handleDocumentDone();

    // 发送缓存的文件数据 套接字连接成功后发送
    Q_INVOKABLE void sendPendingData();

    // 处理套接字可读事件
    void onSocketReadyRead();
    // 处理收到的完整协议数据
    void handleIncomingJsonPacket(const QByteArray &completeData);

    // 单例对象智能指针
    static QScopedPointer<SocketDocWrite> m_instance;
    // 单例初始化互斥锁
    static QMutex s_instanceMutex;
    // 实例状态互斥锁（保护 m_isReleased 与 m_socketThread 创建）
    mutable QMutex m_instanceMutex;

    // 数据接收状态
    RecvState m_recvState = RecvState::WaitHeader;
    // 期望接收的数据长度
    uint32_t m_expectedDataLen = 0;
    // 套接字工作线程
    QThread* m_socketThread = nullptr;
    // 套接字通信对象
    QTcpSocket* m_socket = nullptr;
    // 服务器地址
    QString m_hostName;
    // 服务器端口
    quint16 m_port = 0;
    // 实例是否已释放
    bool m_isReleased = false;
    // 心跳检测定时器
    QTimer* m_timer = nullptr;
    // 是否正在发送文件
    bool m_isFileSending = false;

    // 待发送文件数据
    struct PendingFileData {
        QByteArray data;
        QString filename;
    };
    // 待发送文件数据缓存
    PendingFileData m_pendingFileData;
    // 是否存在缓存待发送数据
    bool m_isPendingData = false;

    // 接收缓存用于拆包
    QByteArray m_incomingBuffer;

    // 上传分片状态 仅用于发送文件
    QFile m_uploadFile;
    QString m_uploadUuid;
    QString m_uploadSender;
    QString m_uploadReceiver;
    QString m_uploadFilename;
    QString m_uploadTimestamp;
    qint64 m_uploadTotalBytes = 0;
    qint64 m_uploadSentBytes = 0;
    int m_lastUploadProgressPercent = -1;
    int m_uploadChunkBytes = 256 * 1024;
    int m_uploadSeq = 0;
    QTimer *m_uploadTimer = nullptr;
    qint64 m_uploadTokens = 0;
    qint64 m_uploadLastRefillMs = 0;
    bool m_uploadRateLimitEnabled = false;
    int m_uploadRateBytesPerSec = 0;
    int m_uploadBurstBytes = 0;
    bool m_uploadIsCloud = false;
    bool m_cloudUploadNotified = false;
};

// 数据接收套接字处理类
// 单例模式独立线程处理通用数据接收通信 与文件发送类对称
class SocketDocRead : public QObject
{
    Q_OBJECT
    template <typename T>
    friend struct QScopedPointerDeleter;

public:
    // 构造函数
    explicit SocketDocRead(QObject* parent = nullptr);

    // 析构函数
    ~SocketDocRead() override;

    // 禁用拷贝构造和赋值运算符 单例模式保护
    SocketDocRead(const SocketDocRead&) = delete;
    SocketDocRead& operator=(const SocketDocRead&) = delete;

    // 获取单例实例
    static SocketDocRead& instance();

    // 线程安全发送数据
    void sendData(const QByteArray& data);

    // 在主线程处理完 document_end / document_error 等收尾逻辑后再调用，避免提前断连导致分片尚未写入
    void scheduleDownloadSessionFinish();

    // 手动释放单例资源 释放后需要重新获取实例
    Q_INVOKABLE static void releaseInstance();

signals:
    // 套接字连接成功信号
    void socketConnected();

    // 套接字断开连接信号
    void socketDisconnected();

    // 套接字错误信号
    void socketError(const QString& errorString);

    // 接收数据信号 完整数据已经解析协议头
    void dataReceived(const QByteArray& data);
    // 接收二进制数据信号
    void binaryReceived(const QByteArray& data);
    // 文档下载流：JSON 与二进制分片合并为同一信号，保证 QueuedConnection 下与 TCP 解析顺序一致（避免 document_end 早于分片到达主线程导致“文件不完整”）
    void docStreamPacket(const QByteArray& data, bool isBinary);


private:
    // 执行套接字操作 跨线程调用接口
    Q_INVOKABLE void executeSocketOp(const QByteArray& data, bool isPending);

    // 清理套接字相关资源 关闭连接释放对象并重置状态
    Q_INVOKABLE void cleanupSocketResources();

    // 处理数据接收完成 数据解析并发送信号后断开连接并清理状态
    Q_INVOKABLE void handleDataReceivedFinished();

    // 发送缓存的数据 套接字连接成功后发送
    Q_INVOKABLE void sendPendingData();

    // 单例对象智能指针
    static QScopedPointer<SocketDocRead> m_instance;
    // 单例初始化互斥锁
    static QMutex s_instanceMutex;
    // 实例状态互斥锁（保护 m_isReleased 与 m_socketThread 创建）
    mutable QMutex m_instanceMutex;

    // 数据接收状态
    RecvState m_recvState = RecvState::WaitHeader;
    // 期望接收的数据长度
    uint32_t m_expectedDataLen = 0;
    // 期望数据类型
    uint8_t m_expectedPayloadType = PAYLOAD_JSON;
    // 套接字工作线程
    QThread* m_socketThread = nullptr;
    // 套接字通信对象
    QTcpSocket* m_socket = nullptr;
    // 服务器地址
    QString m_hostName;
    // 服务器端口
    quint16 m_port = 0;
    // 实例是否已释放
    bool m_isReleased = false;
    // 心跳检测定时器
    QTimer* m_timer = nullptr;
    // 接收数据缓存 未解析完整包
    QByteArray m_jsonDocData;
    // 是否正在接收数据
    bool m_isDataReceiving = false;

    // 待发送数据
    struct PendingData {
        QByteArray data;
    };
    // 待发送数据缓存
    PendingData m_pendingData;
    // 是否存在缓存待发送数据
    bool m_isPendingData = false;
};

#endif // 结束
