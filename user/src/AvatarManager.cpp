/**
 * @file AvatarManager.cpp
 * 头像磁盘与内存缓存加载。
 */
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include <QApplication>
#include <QByteArray>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QBuffer>
#include <QDebug>
#include <QDir>

// 单例对象
AvatarManager* AvatarManager::m_instance = nullptr;

// 构造函数
AvatarManager::AvatarManager(QObject *parent) : QObject(parent)
{
    setDefaultAvatar();
}

// 获取单例实例
AvatarManager *AvatarManager::getInstance()
{
    if (!m_instance) {
        m_instance = new AvatarManager(qApp);
    }
    return m_instance;
}

// 加载头像
QPixmap AvatarManager::loadAvator(const QString& friendId, const QSize& targetSize, int radius)
{
    QString cacheKey = QString("%1x%2_%3")
                           .arg(targetSize.width())
                           .arg(targetSize.height())
                           .arg(radius);

    // 空账号使用默认头像
    if (friendId.isEmpty()) {
        if (!m_defaultAvaCache.processed.contains(cacheKey)) {
            QPixmap scaled = m_defaultAvaCache.original.scaled(
                targetSize,
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation
                );
            m_defaultAvaCache.processed[cacheKey] = getRoundedPixmap(scaled, radius);
        }
        return m_defaultAvaCache.processed[cacheKey];
    }

    auto& cache = m_avatorHash[friendId];

    // 已缓存直接返回
    if (cache.processed.contains(cacheKey)) {
        return cache.processed[cacheKey];
    }

    // 读取原始头像
    if (cache.original.isNull()) {
        AccountInfo friendInfo = AccountMessageManager::getInstance()->getInfo(friendId);
        QString avatorUrl = friendInfo.avatorUrl;

        if (!avatorUrl.isEmpty()) {
            if (QFile::exists(avatorUrl)) {
                cache.original = QPixmap(avatorUrl);
            }
        }

        if (cache.original.isNull()) {
            cache.original = m_defaultAvaCache.original;
        }
    }

    QPixmap processedPixmap;
    if (targetSize.isEmpty() || (targetSize.width() <= 0 && targetSize.height() <= 0)) {
        processedPixmap = cache.original;
    } else {
        processedPixmap = cache.original.scaled(
            targetSize,
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation
            );
    }

    processedPixmap = getRoundedPixmap(processedPixmap, radius);

    cache.processed[cacheKey] = processedPixmap;

    return processedPixmap;
}

// 设置默认头像
void AvatarManager::setDefaultAvatar(const QString &pixmapPath)
{
    m_defaultAvaCache.original = QPixmap(pixmapPath);
    m_defaultAvaCache.processed.clear();
}

// 图片转编码
QString AvatarManager::pixmapToBase64(const QPixmap &pixmap)
{
    QImage image = pixmap.toImage();
    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly)) {
        qWarning() << "AvatarManager: QBuffer open failed";
        return QString();
    }
    if (!image.save(&buffer, "PNG")) {
        qWarning() << "AvatarManager: image save to buffer failed";
        return QString();
    }
    return QString(buffer.data().toBase64());
}

// 编码转图片
QPixmap AvatarManager::base64ToPixmap(const QString &base64Str)
{
    QByteArray byteArray = QByteArray::fromBase64(base64Str.toUtf8());
    QImage image;
    if (!image.loadFromData(byteArray, "PNG") &&
        !image.loadFromData(byteArray, "JPG") &&
        !image.loadFromData(byteArray, "JPEG")) {
        return QPixmap();
    }
    return QPixmap::fromImage(image);
}

// 生成圆角头像
QPixmap AvatarManager::getRoundedPixmap(const QPixmap& srcPixmap, int radius)
{
    if (srcPixmap.isNull()) return QPixmap();
    if (radius <= 0) return srcPixmap;

    QSize desSize = srcPixmap.size();
    QPixmap desPixMap(desSize);
    desPixMap.fill(Qt::transparent);

    QPainter painter(&desPixMap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(0, 0, desSize.width(), desSize.height(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, desSize.width(), desSize.height(), srcPixmap);
    painter.setClipping(false);

    return desPixMap;
}

// 清空头像缓存
void AvatarManager::clearAvatarCache(const QString &friendId)
{
    if (friendId.isEmpty()) {
        m_avatorHash.clear();
    } else {
        m_avatorHash.remove(friendId);
    }
}
