#ifndef SOCKETDEPEND_H
#define SOCKETDEPEND_H

/**
 * @file SocketDepend.h
 * 二进制协议帧头、包长上限与编解码辅助（须与服务端 depend 及测试脚本对齐）。
 */

// 本工程内协议头（部署独立；与对端联调时请对齐 MAX_* 等常量）。

#include <QSysInfo>
#include <cstdint>
#include <QByteArray>
#include <QTcpSocket>
#include <cstring>

// 协议包大小上限，避免被异常/恶意包拖垮内存。
// 说明：JSON 主要用于业务消息；Binary 用于分片/文件帧等二进制负载。
#define MAX_JSON_PACKET_SIZE (16 * 1024 * 1024)
#define MAX_BINARY_PACKET_SIZE (4 * 1024 * 1024)
#define MAX_PACKET_SIZE MAX_JSON_PACKET_SIZE
#define PROTOCOL_HEADER_LEN 8  // 协议头长度

// 协议版本字节（须与 server/depend/SocketDepend.h 及测试脚本一致）
#define PROTOCOL_WIRE_VERSION static_cast<uint8_t>(0xC5)

// 八字节协议头结构体
// 版本号 预留 数据长度
struct ProtocolHeader {
    // 协议版本号
    uint8_t version = PROTOCOL_WIRE_VERSION;
    // 预留字段
    uint8_t reserved[3] = {0};
    // 数据体长度
    uint32_t dataLen = 0;
};

// 前置声明
inline QByteArray headerToBytes(const ProtocolHeader& header);

// 负载类型
enum : uint8_t {
    PAYLOAD_JSON = 0,
    PAYLOAD_BINARY = 1,
};

inline QByteArray makeHeaderBytes(uint32_t dataLen, uint8_t payloadType = PAYLOAD_JSON)
{
    ProtocolHeader h;
    h.dataLen = dataLen;
    h.reserved[0] = payloadType;
    return headerToBytes(h);
}

enum class RecvState {
    // 等待协议头
    WaitHeader,
    // 等待数据体
    WaitData
};

// 主机序转网络序
inline uint32_t hostToNetwork32(uint32_t hostLong)
{
    if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
        return ((hostLong & 0x000000FF) << 24) |
               ((hostLong & 0x0000FF00) << 8)  |
               ((hostLong & 0x00FF0000) >> 8)  |
               ((hostLong & 0xFF000000) >> 24);
    } else {
        return hostLong;
    }
}

// 网络序转主机序
inline uint32_t networkToHost32(uint32_t netLong)
{
    return hostToNetwork32(netLong);
}

// 协议头转字节数组
inline QByteArray headerToBytes(const ProtocolHeader& header) {
    QByteArray bytes(PROTOCOL_HEADER_LEN, 0);
    char* ptr = bytes.data();

    *ptr++ = header.version;
    memcpy(ptr, header.reserved, sizeof(header.reserved));
    ptr += sizeof(header.reserved);
    uint32_t netDataLen = hostToNetwork32(header.dataLen);
    memcpy(ptr, &netDataLen, sizeof(netDataLen));

    return bytes;
}

// 字节数组转协议头
inline ProtocolHeader bytesToHeader(const QByteArray& bytes) {
    ProtocolHeader header;
    if (bytes.length() != PROTOCOL_HEADER_LEN) return header;

    const char* ptr = bytes.constData();
    header.version = *ptr++;
    memcpy(header.reserved, ptr, sizeof(header.reserved));
    ptr += sizeof(header.reserved);
    uint32_t netDataLen = 0;
    memcpy(&netDataLen, ptr, sizeof(netDataLen));
    header.dataLen = networkToHost32(netDataLen);

    return header;
}

// 读取协议头
inline bool readHeader(QTcpSocket* socket, QByteArray& recvBuffer, ProtocolHeader& outHeader) {
    qint64 needRead = PROTOCOL_HEADER_LEN - recvBuffer.length();
    if (needRead > 0) {
        // reserve() 只保证容量，不改变长度；不能直接写 recvBuffer.data()+len。
        const QByteArray tempBuf = socket->read(needRead);
        const qint64 readLen = tempBuf.length();
        if (readLen <= 0) {
            recvBuffer.clear();
            return false;
        }
        recvBuffer.append(tempBuf);
    }

    if (recvBuffer.length() < PROTOCOL_HEADER_LEN) {
        return false;
    }

    outHeader = bytesToHeader(recvBuffer);

    // 校验协议版本号，不匹配则清空并返回失败
    if (outHeader.version != PROTOCOL_WIRE_VERSION) {
        recvBuffer.clear();
        return false;
    }

    // 只移除头部 8 字节，剩余数据保留供后续调用；不再需要额外的 clear
    recvBuffer = recvBuffer.mid(PROTOCOL_HEADER_LEN);
    return true;
}

#endif // SOCKETDEPEND_H
