/**
 * @file AccountMessageManager.cpp
 * 内存中的好友账号信息缓存单例。
 */
#include "AccountMessageManager.h"

// 初始化单例对象
AccountMessageManager* AccountMessageManager::m_instance = nullptr;
AccountInfo AccountMessageManager::m_emptyAccount;

// 构造函数
AccountMessageManager::AccountMessageManager(QObject *parent)
    : QObject(parent)
{
}

// 获取单例实例
AccountMessageManager* AccountMessageManager::getInstance()
{
    if (!m_instance) {
        m_instance = new AccountMessageManager();
    }
    return m_instance;
}

// 获取指定账号信息
AccountInfo& AccountMessageManager::getInfo(const QString& account)
{
    // 检查账号是否存在
    if (m_friendHash.contains(account)) {
        return m_friendHash[account];
    }
    // 不存在返回空对象
    return m_emptyAccount;
}
