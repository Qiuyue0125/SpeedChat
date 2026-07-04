#ifndef AVATARMANAGER_H
#define AVATARMANAGER_H

/**
 * @file AvatarManager.h
 * 头像磁盘加载与处理结果缓存。
 */

#include <QObject>
#include <QPixmap>
#include <QHash>

// 头像缓存结构体
struct AvatarCache {
    // 原始头像
    QPixmap original;
    // 处理头像缓存
    QHash<QString, QPixmap> processed;
};

// 头像管理类
class AvatarManager : public QObject
{
    Q_OBJECT
    // 构造函数
    explicit AvatarManager(QObject *parent = nullptr);
    // 单例对象
    static AvatarManager *m_instance;

public:
    // 获取单例实例
    static AvatarManager* getInstance();

    // 加载头像
    QPixmap loadAvator(const QString& friendId, const QSize& targetSize, int radius = 0);

    // 设置默认头像
    void setDefaultAvatar(const QString& pixmapPath = ":/pictures/suliao_avator_normal.jpg");

    // 图片转编码
    static QString pixmapToBase64(const QPixmap &pixmap);

    // 编码转图片
    static QPixmap base64ToPixmap(const QString &base64Str);

    // 生成圆角头像
    static QPixmap getRoundedPixmap(const QPixmap& srcPixmap, int radius);

    // 清空头像缓存
    void clearAvatarCache(const QString& friendId = "");

private:
    // 好友头像缓存
    QHash<QString, AvatarCache> m_avatorHash;
    // 默认头像缓存
    AvatarCache m_defaultAvaCache;
};

#endif // 结束
