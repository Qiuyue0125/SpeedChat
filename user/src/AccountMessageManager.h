#ifndef ACCOUNTMESSAGEMANAGER_H
#define ACCOUNTMESSAGEMANAGER_H

/**
 * @file AccountMessageManager.h
 * 好友账号信息内存缓存（单例）。
 */

#include <QObject>
#include <QHash>
#include <QString>

// 账号信息结构体
struct AccountInfo {
    // 账号
    QString account;
    // 名称
    QString name;
    // 头像编码
    QString avator_base64;
    // 头像路径
    QString avatorUrl;
    // 性别
    QString gender;
    // 签名
    QString signature;
};

// 账号信息管理类
class AccountMessageManager : public QObject
{
    Q_OBJECT

public:
    // 获取单例实例
    static AccountMessageManager* getInstance();

    // 禁止拷贝和赋值
    AccountMessageManager(const AccountMessageManager&) = delete;
    AccountMessageManager& operator=(const AccountMessageManager&) = delete;

    // 插入账号信息
    void insert(const QString &account, const AccountInfo& info){
        m_friendHash.insert(account, info);
    }
    // 删除账号信息
    void remove(const QString &account){
        m_friendHash.remove(account);
    }
    // 获取所有账号
    QList<QString> getKeys(){
        return m_friendHash.keys();
    }
    // 获取数量
    int getSize(){
        return m_friendHash.size();
    }
    // 判断是否存在
    bool containAccount(const QString &account){
        return m_friendHash.contains(account);
    }

    // 获取指定账号信息
    AccountInfo& getInfo(const QString& account);

private:
    // 构造函数
    explicit AccountMessageManager(QObject *parent = nullptr);

    // 好友信息表
    QHash<QString, AccountInfo> m_friendHash;
    // 单例对象
    static AccountMessageManager* m_instance;
    // 空账号信息
    static AccountInfo m_emptyAccount;
};

#endif // 结束
