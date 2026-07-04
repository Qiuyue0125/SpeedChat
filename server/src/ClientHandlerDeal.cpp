/**
 * @file ClientHandlerDeal.cpp
 * ClientHandler 业务实现：登录、好友、消息、文件、AI 等业务处理。
 */
#include "ClientHandler.h"
#include "ClientHandlerShared.h"
#include "ConnectionPool.h"
#include "ServerConfigDefaults.h"
#include "SocketDepend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSharedPointer>
#include <QUuid>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {

// 聊天 AI 分析按账号限流：记录各账号上次成功进入线程池任务的时间戳（毫秒）。
QMutex g_aiAnalyzeRateMutex;
QHash<QString, qint64> g_aiAnalyzeAccountLastStartMs;

// 将客户端传来的时间单位规范为 hour / day / month；无法识别则返回空串。
QString normalizeAiRangeUnit(const QString &u)
{
    QString t = u.trimmed().toLower();
    // 自定义区间：如 hour_custom/day_custom/month_custom
    if (t.endsWith(QStringLiteral("_custom"))) {
        t = t.left(t.size() - QStringLiteral("_custom").size());
    }
    if (t == QLatin1String("hour") || t == QLatin1String("h") || t == QStringLiteral("小时")) {
        return QStringLiteral("hour");
    }
    if (t == QLatin1String("day") || t == QLatin1String("d") || t == QStringLiteral("天")) {
        return QStringLiteral("day");
    }
    if (t == QLatin1String("month") || t == QLatin1String("m") || t == QStringLiteral("月")) {
        return QStringLiteral("month");
    }
    return QString();
}

// 由结束时间与数值、单位推算聊天记录查询的起始时间。
QDateTime computeRangeStart(const QDateTime &end, int value, const QString &unitNorm)
{
    if (!end.isValid() || value <= 0) return {};
    if (unitNorm == QLatin1String("hour")) {
        return end.addSecs(-3600 * value);
    }
    if (unitNorm == QLatin1String("day")) {
        return end.addDays(-value);
    }
    if (unitNorm == QLatin1String("month")) {
        return end.addMonths(-value);
    }
    return {};
}

// 将单条消息格式化为送入大模型的文本行（含时间、收发角色、类型摘要）。
QString formatChatLineForAi(const QString &selfAccount, const QString &sender, const QString &msgType,
                            const QString &content, const QString &filename, const QString &timestamp)
{
    const QString who = (sender == selfAccount) ? QStringLiteral("我") : QStringLiteral("对方");
    QString text;
    if (msgType == QLatin1String("document")) {
        text = QStringLiteral("[文件: %1]").arg(filename.isEmpty() ? content : filename);
    } else if (msgType == QLatin1String("picture")) {
        text = QStringLiteral("[图片]");
    } else if (msgType == QLatin1String("audio")) {
        text = QStringLiteral("[语音]");
    } else {
        text = content;
    }
    return QStringLiteral("[%1] %2: %3\n").arg(timestamp, who, text);
}

// 同步调用 OpenAI 兼容 Chat Completions HTTP 接口，成功返回助手文本，失败时写入 outError。
QString callOpenAiCompatibleChat(const QUrl &url, const QString &apiKey, const QString &model,
                                 const QString &systemPrompt, const QString &userContent, int timeoutMs,
                                 QString *outError)
{
    if (outError) outError->clear();
    if (!url.isValid() || apiKey.isEmpty()) {
        if (outError) *outError = QStringLiteral("invalid_api_config");
        return QString();
    }

    QJsonObject root;
    root["model"] = model;
    root["temperature"] = 0.5;
    root["max_tokens"] = 4096;
    QJsonArray msgs;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sys;
        sys["role"] = QStringLiteral("system");
        sys["content"] = systemPrompt;
        msgs.append(sys);
    }
    QJsonObject usr;
    usr["role"] = QStringLiteral("user");
    usr["content"] = userContent;
    msgs.append(usr);
    root["messages"] = msgs;

    const QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ChatServer/1.0"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());

    QNetworkReply *reply = nam.post(req, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(qMax(1000, timeoutMs));
    loop.exec();
    timer.stop();

    const QByteArray respBytes = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError nerr = reply->error();
    reply->close();
    delete reply;

    const bool badNet = (nerr != QNetworkReply::NoError);
    const bool badHttp = (httpStatus > 0 && httpStatus >= 400);
    if (badNet || badHttp) {
        if (outError) {
            QString apiMsg;
            const QJsonDocument ej = QJsonDocument::fromJson(respBytes);
            if (ej.isObject()) {
                apiMsg = ej.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            }
            if (!apiMsg.isEmpty()) {
                *outError = apiMsg;
            } else {
                *outError = QStringLiteral("http_%1: %2")
                                .arg(httpStatus)
                                .arg(QString::fromUtf8(respBytes.left(512)));
            }
        }
        return QString();
    }

    QJsonParseError pe;
    const QJsonDocument jd = QJsonDocument::fromJson(respBytes, &pe);
    if (jd.isNull() || !jd.isObject()) {
        if (outError) *outError = QStringLiteral("bad_json_response");
        return QString();
    }
    const QJsonObject jo = jd.object();
    if (jo.contains(QStringLiteral("error"))) {
        const QJsonObject er = jo.value(QStringLiteral("error")).toObject();
        if (outError) {
            *outError = er.value(QStringLiteral("message")).toString();
            if (outError->isEmpty()) *outError = QStringLiteral("api_error");
        }
        return QString();
    }
    const QJsonArray choices = jo.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        if (outError) *outError = QStringLiteral("empty_choices");
        return QString();
    }
    const QJsonObject c0 = choices.at(0).toObject();
    const QJsonObject message = c0.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (content.isEmpty() && message.contains(QStringLiteral("reasoning_content"))) {
        return message.value(QStringLiteral("reasoning_content")).toString();
    }
    return content;
}

QByteArray makeDocDownloadReadyPayload(const QString &uuidStr,
                                       const QString &filePath,
                                       const QString &filename,
                                       qint64 totalBytes,
                                       int chunkBytes)
{
    QJsonObject ok;
    ok["tag"] = "_doc_download_ready";
    ok["uuid"] = uuidStr;
    ok["file_path"] = filePath;
    ok["filename"] = filename;
    ok["total_bytes"] = QString::number(totalBytes);
    ok["chunk_bytes"] = chunkBytes;
    return ClientHandlerShared::toJsonCompactBytes(ok);
}


// Qt 6 QMYSQL 事务兼容层：驱动层 autocommit 状态追踪异常，统一使用裸 SQL。
static bool qSqlStartTransaction(QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec("START TRANSACTION");
}
static bool qSqlCommit(QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec("COMMIT");
}
static bool qSqlRollback(QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec("ROLLBACK");
}

}  // namespace

// ===== 上传 =====
// 处理上传开始（路由入口）
void ClientHandler::dealUploadBegin(const QJsonObject &json)
{
    const QString uuid = json.value("uuid").toString();
    const QString sender = json.value("sender").toString();
    const QString receiver = json.value("receiver").toString();
    const QString filename = json.value("filename").toString();
    // 网盘上传：cloud=true 或 sender==receiver 时进入网盘模式（不写 Messages，落 CloudFiles）
    const bool cloudFlag = json.value("cloud").toBool();
    const bool isCloud = cloudFlag || (!sender.isEmpty() && sender == receiver);
    QString messageType = json.value("messagetype").toString("document");
    const QString timestamp = json.value("timestamp").toString();
    const qint64 totalBytes = json.value("total_bytes").toString().toLongLong();

    if (isCloud) {
        messageType = QStringLiteral("cloud");
        if (uuid.isEmpty() || sender.isEmpty() || filename.isEmpty() || totalBytes <= 0) {
            QJsonObject err;
            err["tag"] = "upload_error";
            err["uuid"] = uuid;
            err["message"] = "invalid cloud upload fields";
            enqueueDocPacket(err);
            return;
        }
    } else if (uuid.isEmpty() || sender.isEmpty() || receiver.isEmpty() || filename.isEmpty() || totalBytes <= 0) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "invalid begin fields";
        enqueueDocPacket(err);
        return;
    }
    if (!isCloud && messageType != "document" && messageType != "audio" && messageType != "picture") {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "unsupported messagetype";
        enqueueDocPacket(err);
        return;
    }

    if (m_uploads.size() >= ServerConfigDefaults::defaultMaxConcurrentUploadsPerConn() && !m_uploads.contains(uuid)) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "too many concurrent uploads";
        enqueueDocPacket(err);
        return;
    }
    if (m_uploads.contains(uuid)) {
        // 覆盖旧会话
        UploadSession &old = m_uploads[uuid];
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, old.filePath);
        m_uploads.remove(uuid);
        updateUploadCleanupRegistration();
    }

    const QString suffix = QFileInfo(filename).suffix();
    const QString uniqueFileName = suffix.isEmpty() ? uuid : (uuid + "." + suffix);
    const QString fileUrl = messageType + "/" + uniqueFileName;
    const QString basePath = ServerConfigDefaults::storageBasePath();
    const QString filePath = basePath + fileUrl;
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [filePath]() -> QByteArray {
            QDir dir(QFileInfo(filePath).absolutePath());
            if (!dir.exists() && !dir.mkpath(".")) {
                return QByteArray();
            }
            QFile initFile(filePath);
            if (!initFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return QByteArray();
            }
            initFile.close();
            return QByteArray("1");
        },
        [thisPtr, uuid, sender, receiver, filename, messageType, timestamp, totalBytes, filePath, fileUrl, isCloud](const QByteArray &prepared) {
            if (thisPtr.isNull()) {
                // 连接已断但 DB 线程已创建空文件：未进入 m_uploads，onDisconnected 不会删，此处补删。
                if (prepared == QByteArray("1") && !filePath.isEmpty()) {
                    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
                        QCoreApplication::instance(),
                        [filePath]() { QFile::remove(filePath); });
                }
                return;
            }
            if (prepared.isEmpty()) {
                QJsonObject err;
                err["tag"] = "upload_error";
                err["uuid"] = uuid;
                err["message"] = "failed to open file for write";
                thisPtr->enqueueDocPacket(err);
                return;
            }

            UploadSession session;
            session.active = true;
            session.uuid = uuid;
            session.sender = sender;
            session.receiver = isCloud ? sender : receiver;
            session.filename = filename;
            session.messageType = messageType;
            session.timestamp = timestamp;
            session.totalBytes = totalBytes;
            session.receivedBytes = 0;
            session.chunkBytes = ServerConfigDefaults::serverUploadChunkBytes();
            session.filePath = filePath;
            session.fileUrl = fileUrl;
            session.lastActivityMs = QDateTime::currentMSecsSinceEpoch();
            session.isCloud = isCloud;
            const int chunkBytes = session.chunkBytes;
            thisPtr->m_uploads.insert(uuid, std::move(session));
            thisPtr->updateUploadCleanupRegistration();

            QJsonObject ack;
            ack["tag"] = "upload_begin_ack";
            ack["uuid"] = uuid;
            ack["messagetype"] = messageType;
            ack["chunk_bytes"] = chunkBytes;
            ack["received_bytes"] = QString::number(0);
            ack["total_bytes"] = QString::number(totalBytes);
            thisPtr->enqueueDocPacket(ack);
        });
}

// 处理上传分片（路由入口）
void ClientHandler::dealUploadChunk(const QJsonObject &json)
{
    const QString uuid = json.value("uuid").toString();
    static std::atomic<quint64> sJsonUploadChunkLogCounter{0};
    if (ClientHandlerShared::shouldSample(sJsonUploadChunkLogCounter, 256)) {
        qWarning() << "【连接处理】收到JSON upload_chunk，已拒绝，账号" << m_accountId;
    }
    QJsonObject err;
    err["tag"] = "upload_error";
    err["uuid"] = uuid;
    err["message"] = "json upload_chunk is not supported";
    enqueueDocPacket(err);
}

// 处理上传结束（路由入口）
void ClientHandler::dealUploadEnd(const QJsonObject &json)
{
    const QString uuid = json.value("uuid").toString();
    if (uuid.isEmpty() || !m_uploads.contains(uuid)) {
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = "上传会话不存在";
        enqueueDocPacket(err);
        return;
    }
    UploadSession &s = m_uploads[uuid];
    if (s.endRequested) {
        return;
    }
    s.endRequested = true;
    if (s.writeInFlight || !s.pendingChunks.isEmpty()
        || m_binaryParseInFlight || !m_binaryParseQueue.isEmpty()) {
        return;
    }
    if (s.receivedBytes != s.totalBytes) {
        finishUploadSession(uuid, false, "size mismatch");
        return;
    }
    finishUploadSession(uuid, true);
}

// 结束上传会话（成功/失败）
void ClientHandler::finishUploadSession(const QString &uuid, bool success, const QString &message)
{
    if (!m_uploads.contains(uuid))
        return;

    // 把上传会话复制一份（拿到文件路径）
    const UploadSession s = m_uploads.value(uuid);
    const QString filePath = s.filePath;

    // 从内存的上传列表中删除这个会话
    m_uploads.remove(uuid);

    // 更新超时清理器（不再监控这个会话）
    updateUploadCleanupRegistration();

    // 上传失败的处理
    if (!success) {
        // 线程池异步删除临时文件（不阻塞主线程）
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, filePath);

        // 给客户端返回错误消息
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = message.isEmpty() ? "upload failed" : message;
        enqueueDocPacket(err);
        return;
    }

    // 成功前做一次落盘完整性校验，避免产生"消息入库但文件0字节/不完整"的坏数据。
    const qint64 diskSize = QFileInfo(filePath).exists() ? QFileInfo(filePath).size() : -1;
    if (diskSize <= 0 || (s.totalBytes > 0 && diskSize != s.totalBytes)) {
        const QString reason = (diskSize <= 0)
            ? QStringLiteral("file empty on disk")
            : QStringLiteral("disk size mismatch");
        ClientHandlerShared::scheduleFileRemoveOnThreadPool(this, filePath);
        QJsonObject err;
        err["tag"] = "upload_error";
        err["uuid"] = uuid;
        err["message"] = reason;
        enqueueDocPacket(err);
        return;
    }

    QPointer<ClientHandler> thisPtr(this);
    // 网盘：仅 INSERT CloudFiles，回 cloud_upload_done + uploaddone
    if (s.isCloud) {
        ClientHandlerShared::runDbTask(
            this,
            [uuid, s]() -> QByteArray {
                QUuid qu = QUuid::fromString(uuid);
                if (qu.isNull()) qu = QUuid::fromString("{" + uuid + "}");
                if (qu.isNull()) {
                    QJsonObject err;
                    err["tag"] = "upload_error";
                    err["uuid"] = uuid;
                    err["message"] = "invalid uuid";
                    return ClientHandlerShared::toJsonCompactBytes(err);
                }

                QSqlDatabase db = ConnectionPool::getInstance().getConnection();
                DbConnectionGuard guard(db);
                if (!db.isValid() || !db.isOpen()) {
                    QJsonObject err;
                    err["tag"] = "upload_error";
                    err["uuid"] = uuid;
                    err["message"] = "db connection invalid";
                    return ClientHandlerShared::toJsonCompactBytes(err);
                }
                if (!qSqlStartTransaction(db)) {
                    QJsonObject err;
                    err["tag"] = "upload_error";
                    err["uuid"] = uuid;
                    err["message"] = "db transaction failed";
                    return ClientHandlerShared::toJsonCompactBytes(err);
                }

                QSqlQuery qry(db);
                qry.prepare("INSERT INTO CloudFiles (file_id, owner_id, filename, content, file_size, timestamp) "
                            "VALUES (:file_id, :owner, :filename, :content, :file_size, :timestamp)");
                qry.bindValue(":file_id", qu.toRfc4122());
                qry.bindValue(":owner", s.sender);
                qry.bindValue(":filename", s.filename);
                qry.bindValue(":content", s.fileUrl);
                qry.bindValue(":file_size", s.totalBytes);
                qry.bindValue(":timestamp", s.timestamp);

                if (!qry.exec() || !qSqlCommit(db)) {
                    qSqlRollback(db);
                    QJsonObject err;
                    err["tag"] = "upload_error";
                    err["uuid"] = uuid;
                    err["message"] = "db insert failed";
                    return ClientHandlerShared::toJsonCompactBytes(err);
                }

                QJsonObject ok;
                ok["tag"] = "cloud_upload_ok";
                ok["uuid"] = uuid;
                return ClientHandlerShared::toJsonCompactBytes(ok);
            },
            [thisPtr, uuid, s, filePath](const QByteArray &result) {
                if (thisPtr.isNull()) return;
                QJsonParseError e{};
                const QJsonDocument doc = QJsonDocument::fromJson(result, &e);
                const QJsonObject obj = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
                const QString tag = obj.value("tag").toString();
                if (tag != "cloud_upload_ok") {
                    ClientHandlerShared::scheduleFileRemoveOnThreadPool(thisPtr, filePath);
                    QJsonObject err = obj;
                    if (!err.contains("tag")) err["tag"] = "upload_error";
                    err["uuid"] = uuid;
                    thisPtr->enqueueDocPacket(err);
                    return;
                }

                QJsonObject done;
                done["tag"] = "cloud_upload_done";
                done["file_id"] = uuid;
                done["filename"] = s.filename;
                done["file_size"] = QString::number(s.totalBytes);
                thisPtr->enqueueDocPacket(done);

                QJsonObject uploadDone;
                uploadDone["tag"] = "uploaddone";
                uploadDone["messagetype"] = s.messageType;
                uploadDone["uuid"] = uuid;
                thisPtr->enqueueDocPacket(uploadDone);
            });
        return;
    }

    ClientHandlerShared::runDbTask(
        this,
        [uuid, s]() -> QByteArray {
            const QString sender = s.sender;
            const QString receiver = s.receiver;
            const QString messageType = s.messageType;
            const QString timestamp = s.timestamp;
            const QString filename = s.filename;
            const QString fileUrl = s.fileUrl;
            QUuid qu = QUuid::fromString(uuid);
            if (qu.isNull()) qu = QUuid::fromString("{" + uuid + "}");
            if (qu.isNull()) {
                QJsonObject err;
                err["tag"] = "upload_error";
                err["uuid"] = uuid;
                err["message"] = "invalid uuid";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                QJsonObject err;
                err["tag"] = "upload_error";
                err["uuid"] = uuid;
                err["message"] = "db connection invalid";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            if (!qSqlStartTransaction(db)) {
                QJsonObject err;
                err["tag"] = "upload_error";
                err["uuid"] = uuid;
                err["message"] = "db transaction failed";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            QSqlQuery qry(db);
            qry.prepare("INSERT INTO Messages (message_id, sender_id, receiver_id, content, message_type, status, timestamp, filename) "
                        "VALUES (:message_id, :sender, :receiver, :content, :messagetype, :status, :timestamp, :filename)");
            qry.bindValue(":message_id", qu.toRfc4122());
            qry.bindValue(":sender", sender);
            qry.bindValue(":receiver", receiver);
            qry.bindValue(":content", fileUrl);
            qry.bindValue(":messagetype", messageType);
            qry.bindValue(":status", "unread");
            qry.bindValue(":timestamp", timestamp);
            qry.bindValue(":filename", filename);

            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                QJsonObject err;
                err["tag"] = "upload_error";
                err["uuid"] = uuid;
                err["message"] = "db insert failed";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            QJsonObject ok;
            ok["tag"] = "upload_db_ok";
            ok["uuid"] = uuid;
            return ClientHandlerShared::toJsonCompactBytes(ok);
        },
        [thisPtr, uuid, s, filePath](const QByteArray &result) {
            if (thisPtr.isNull()) return;
            QJsonParseError e{};
            const QJsonDocument doc = QJsonDocument::fromJson(result, &e);
            const QJsonObject obj = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
            const QString tag = obj.value("tag").toString();
            if (tag != "upload_db_ok") {
                ClientHandlerShared::scheduleFileRemoveOnThreadPool(thisPtr, filePath);
                QJsonObject err = obj;
                if (!err.contains("tag")) err["tag"] = "upload_error";
                err["uuid"] = uuid;
                thisPtr->enqueueDocPacket(err);
                return;
            }

            QJsonObject mh;
            mh["tag"] = "messagehavedone";
            mh["uuid"] = uuid;
            thisPtr->enqueueDocPacket(mh);

            QJsonObject done;
            done["tag"] = "uploaddone";
            done["messagetype"] = s.messageType;
            done["uuid"] = uuid;
            thisPtr->enqueueDocPacket(done);

            quint64 receiverId = 0;
            if (s.sender == s.receiver) {
                return;
            }
            if (!ClientHandler::tryParseAccountId(s.receiver, receiverId)) return;
            QPointer<ClientHandler> it = thisPtr->getClient(receiverId);
            if (!it.isNull()) {
                if (s.messageType == "audio" || s.messageType == "picture") {
                    QPointer<ClientHandler> receiverPtr = it;
                    auto encoded = QSharedPointer<QByteArray>::create();
                    auto readDone = QSharedPointer<bool>::create(false);
                    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
                        thisPtr,
                        [mediaPath = s.filePath, encoded, readDone]() {
                            QFile mediaFile(mediaPath);
                            if (!mediaFile.open(QIODevice::ReadOnly)) {
                                *readDone = true;
                                return;
                            }
                            *encoded = mediaFile.readAll().toBase64();
                            mediaFile.close();
                            *readDone = true;
                        },
                        [thisPtr, receiverPtr, uuid, s, encoded, readDone]() {
                            if (!*readDone) return;
                            if (thisPtr.isNull() || receiverPtr.isNull()) return;
                            QJsonObject msg;
                            msg["tag"] = "yourmessages";
                            msg["sender"] = s.sender;
                            msg["messagetype"] = s.messageType;
                            msg["receiver"] = s.receiver;
                            msg["timestamp"] = s.timestamp;
                            msg["filename"] = s.filename;
                            msg["messages"] = QString::fromLatin1(*encoded);
                            msg["uuid"] = uuid;
                            const QByteArray msgBytes = ClientHandlerShared::toJsonCompactBytes(msg);
                            receiverPtr->sendJsonToSocketQueued(msgBytes);
                        });
                    return;
                }

                QJsonObject msg;
                msg["tag"] = "yourmessages";
                msg["sender"] = s.sender;
                msg["messagetype"] = s.messageType;
                msg["receiver"] = s.receiver;
                msg["timestamp"] = s.timestamp;
                msg["filename"] = s.filename;
                if (s.messageType == "document") {
                    msg["messages"] = s.filename;
                    msg["need_download"] = true;
                } else {
                    msg["messages"] = s.filename;
                }
                msg["uuid"] = uuid;
                const QByteArray msgBytes = ClientHandlerShared::toJsonCompactBytes(msg);
                it->sendJsonToSocketQueued(msgBytes);
            }
        });
}

// ===== 登录 / 注册 / 找回密码 =====
// 处理登录请求（校验账号密码，返回 loginsucceed/loginfaill）。
void ClientHandler::dealLogin(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account_number"].toString();
            const QString password = json["password"].toString();

            // 2) 参数校验
            if (account.isEmpty() || password.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("login", "loginfaill");
            }

            // 3) DB 访问
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return ClientHandlerShared::makeAnswerAction("login", "loginfaill");
            }

            QSqlQuery qry(db);
            qry.prepare("SELECT password_salt, password FROM Users WHERE account_number = :account_number");
            qry.bindValue(":account_number", account);
            if (!qry.exec() || !qry.next()) {
                return ClientHandlerShared::makeAnswerAction("login", "loginfaill");
            }

            const QString salt = qry.value(0).toString();
            const QString cipherPwd = qry.value(1).toString();
            const bool ok = (ClientHandlerShared::sha256Hex(password, salt) == cipherPwd);

            // 4) Action 返回
            return ClientHandlerShared::makeAnswerAction("login", ok ? "loginsucceed" : "loginfaill");
        },
        [thisPtr](const WorkerActionList &messageData) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(messageData);
        }
        );
}

// 处理注册请求（生成账号、写入用户表、保存头像）。
void ClientHandler::dealRegister(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString nickname = json["nickname"].toString();
            const QString gender = json["gender"].toString();
            const QString password = json["password"].toString();
            const QString question = json["question"].toString();
            const QString answer = json["answer"].toString();
            const QString avatorBase64 = json["avator"].toString();

            // 2) 参数校验
            if (nickname.isEmpty() || gender.isEmpty() || password.isEmpty() ||
                question.isEmpty() || answer.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("register", "regisfail");
            }

            auto failRegister = []() -> WorkerActionList {
                return ClientHandlerShared::makeAnswerAction("register", "regisfail");
            };

            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return failRegister();
            if (!qSqlStartTransaction(db)) return failRegister();

            QString randomNumber;
            const int maxRetry = 100;
            int retryCount = 0;
            bool accountFound = false;
            while (retryCount < maxRetry) {
                retryCount++;
                randomNumber = QString::number(1);
                for (int i = 1; i < 10; ++i)
                    randomNumber.append(QString::number(QRandomGenerator::global()->bounded(0, 10)));

                QSqlQuery checkQry(db);
                checkQry.prepare("SELECT COUNT(*) FROM Users WHERE account_number = :account_number FOR UPDATE");
                checkQry.bindValue(":account_number", randomNumber);
                if (!checkQry.exec() || !checkQry.next()) {
                    qSqlRollback(db);
                    return failRegister();
                }
                if (checkQry.value(0).toInt() == 0) {
                    accountFound = true;
                    break;
                }
            }

            if (!accountFound) {
                qSqlRollback(db);
                return failRegister();
            }

            QString avaFileUrl;
            qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
            QString avaFileName = QString("%1_%2.png").arg(randomNumber).arg(timestamp);
            if (!avatorBase64.isEmpty()) {
                QString avaSubDir = savePath + "ava/";
                QDir avaDir(avaSubDir);
                if (!avaDir.exists()) avaDir.mkpath(".");
                QString avaFilePath = avaSubDir + avaFileName;
                avaFileUrl = "ava/" + avaFileName;
                QByteArray avaData = QByteArray::fromBase64(avatorBase64.toUtf8());
                QFile avaFile(avaFilePath);
                if (!avaFile.open(QIODevice::WriteOnly) || avaFile.write(avaData) != avaData.size()) {
                    avaFile.close();
                    QFile::remove(avaFilePath);
                    qSqlRollback(db);
                    return failRegister();
                }
                avaFile.close();
            }

            QString pwdSalt = ClientHandlerShared::generateSaltStatic();
            QString cipherPwd = ClientHandlerShared::encryptWithSaltStatic(password, pwdSalt);
            QString answerSalt = ClientHandlerShared::generateSaltStatic();
            QString cipherAnswer = ClientHandlerShared::encryptWithSaltStatic(answer, answerSalt);

            QSqlQuery qry(db);
            qry.prepare("INSERT INTO Users (account_number, nickname, signature, gender, password, password_salt, question, answer, answer_salt, avator) "
                        "VALUES (:account_number, :nickname, :signature, :gender, :password, :password_salt, :question, :answer, :answer_salt, :avator)");
            qry.bindValue(":account_number", randomNumber);
            qry.bindValue(":nickname", nickname);
            qry.bindValue(":signature", QString("快和我聊天吧。"));
            qry.bindValue(":gender", gender);
            qry.bindValue(":password", cipherPwd);
            qry.bindValue(":password_salt", pwdSalt);
            qry.bindValue(":question", question);
            qry.bindValue(":answer", cipherAnswer);
            qry.bindValue(":answer_salt", answerSalt);
            qry.bindValue(":avator", avaFileName);

            // 4) Action 返回
            if (!qry.exec()) {
                qSqlRollback(db);
                if (!avaFileUrl.isEmpty()) QFile::remove(savePath + avaFileUrl);
                return failRegister();
            }
            if (!qSqlCommit(db)) {
                qSqlRollback(db);
                if (!avaFileUrl.isEmpty()) QFile::remove(savePath + avaFileUrl);
                return failRegister();
            }

            QJsonObject responseJson;
            responseJson["tag"] = "register";
            responseJson["answer"] = "regissucceed";
            responseJson["account_number"] = randomNumber;
            return ClientHandlerShared::encodeSingleSendSelf(responseJson);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理找回密码步骤一（校验账号存在，返回密保问题）。
void ClientHandler::dealFindpassword1(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account_number"].toString();

            // 2) 参数校验
            if (account.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("findpassword1_answer", "no");
            }

            // 3) DB 访问
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return ClientHandlerShared::makeAnswerAction("findpassword1_answer", "no");
            }

            QSqlQuery qry(db);
            qry.prepare("SELECT COUNT(*),question FROM Users WHERE account_number = :account_number");
            qry.bindValue(":account_number", account);
            if (!qry.exec()) {
                return ClientHandlerShared::makeAnswerAction("findpassword1_answer", "no");
            }

            // 4) Action 返回
            QJsonObject resp;
            resp["tag"] = "findpassword1_answer";
            if (qry.next() && qry.value(0).toInt() == 1) {
                resp["answer"] = "yes";
                resp["question"] = qry.value(1).toString();
            } else {
                resp["answer"] = "no";
            }
            return ClientHandlerShared::encodeSingleSendSelf(resp);
        },
        [thisPtr](const WorkerActionList &data) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(data);
        });
}

// 处理找回密码步骤二（校验密保答案）。
void ClientHandler::dealFindpassword2(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account_number"].toString();
            const QString theAnswer = json["theanswer"].toString();

            // 2) 参数校验
            if (account.isEmpty() || theAnswer.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("findpassword2_answer", "no");
            }

            // 3) DB 访问
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return ClientHandlerShared::makeAnswerAction("findpassword2_answer", "no");
            }

            QSqlQuery qry(db);

            qry.prepare("SELECT answer_salt, answer FROM Users WHERE account_number = :account_number");
            qry.bindValue(":account_number", account);
            if (!qry.exec() || !qry.next()) {
                return ClientHandlerShared::makeAnswerAction("findpassword2_answer", "no");
            }

            // 4) Action 返回
            const QString answerSalt = qry.value(0).toString();
            const QString cipherAnswer = qry.value(1).toString();
            QJsonObject resp;
            resp["tag"] = "findpassword2_answer";
            resp["answer"] = (ClientHandlerShared::sha256Hex(theAnswer, answerSalt) == cipherAnswer) ? "yes" : "no";
            return ClientHandlerShared::encodeSingleSendSelf(resp);
        },
        [thisPtr](const WorkerActionList &data) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(data);
        });
}

// 处理找回密码步骤三（校验答案后更新新密码）。
void ClientHandler::dealFindpassword3(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account_number"].toString();
            const QString theAnswer = json["theanswer"].toString();
            const QString newPassword = json["password"].toString();

            // 2) 参数校验
            if (account.isEmpty() || theAnswer.isEmpty() || newPassword.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("findpassword3_answer", "no");
            }

            auto fail = []() -> WorkerActionList {
                return ClientHandlerShared::makeAnswerAction("findpassword3_answer", "no");
            };

            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return fail();

            QSqlQuery qry(db);
            if (!qSqlStartTransaction(db)) {
                return fail();
            }
            qry.prepare("SELECT password_salt, answer_salt, answer FROM Users WHERE account_number = :account_number");
            qry.bindValue(":account_number", account);
            if (!qry.exec() || !qry.next()) {
                qSqlRollback(db);
                return fail();
            }
            QString answerSalt = qry.value(1).toString();
            QString cipherAnswer = qry.value(2).toString();
            if (!ClientHandlerShared::verifyWithSaltStatic(theAnswer, answerSalt, cipherAnswer)) {
                qSqlRollback(db);
                return fail();
            }
            QString newPwdSalt = ClientHandlerShared::generateSaltStatic();
            QString newCipherPwd = ClientHandlerShared::encryptWithSaltStatic(newPassword, newPwdSalt);
            qry.prepare("UPDATE Users SET password = :password, password_salt = :password_salt WHERE account_number = :account_number");
            qry.bindValue(":password", newCipherPwd);
            qry.bindValue(":password_salt", newPwdSalt);
            qry.bindValue(":account_number", account);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                return fail();
            }

            // 4) Action 返回
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "findpassword3_answer";
            qjsonObj["answer"] = "yes";
            return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
        },
        [thisPtr](const WorkerActionList &data) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(data);
        });
}

// ===== 登录初始化 =====
// 处理登录初始化步骤零（拉取好友列表及头像）。
void ClientHandler::dealLoginMes0(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            // 2) 参数校验
            if (account.isEmpty()) return WorkerActionList();
            // 3) DB 查询
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return WorkerActionList();
            QSqlQuery qry(db);
            qry.setForwardOnly(true);
            qry.prepare("SELECT u.account_number, u.avator "
                        "FROM Friends f "
                        "JOIN Users u ON u.account_number = f.friend_id "
                        "WHERE f.user_id = :user_id");
            qry.bindValue(":user_id", account);
            if (!qry.exec()) return WorkerActionList();
            QJsonArray friendsArray;
            while (qry.next()) {
                QJsonObject friendJson;
                friendJson["account_number"] = qry.value("account_number").toString();
                friendJson["avator_url"] = qry.value("avator").toString();
                friendsArray.append(friendJson);
            }
            QSqlQuery selfQuery(db);
            selfQuery.setForwardOnly(true);
            selfQuery.prepare("SELECT account_number, avator FROM Users WHERE account_number = :self_account");
            selfQuery.bindValue(":self_account", account);
            if (selfQuery.exec() && selfQuery.next()) {
                QJsonObject selfJson;
                selfJson["account_number"] = selfQuery.value("account_number").toString();
                selfJson["avator_url"] = selfQuery.value("avator").toString();
                friendsArray.append(selfJson);
            }
            // 4) Action 返回
            QJsonObject finalJson;
            finalJson["tag"] = "loginmessage0";
            finalJson["friends"] = friendsArray;
            return ClientHandlerShared::encodeSingleSendSelf(finalJson);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理登录初始化步骤一（拉取待下载头像、好友资料）。
void ClientHandler::dealLoginMes1(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            QSet<QString> needDownloadAccounts;
            if (json.contains("needdownload") && json["needdownload"].isArray()) {
                for (const QJsonValue &val : json["needdownload"].toArray())
                    if (val.isString()) needDownloadAccounts.insert(val.toString());
            }
            // 2) 参数校验
            if (account.isEmpty()) return WorkerActionList();
            // 3) DB 查询
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return WorkerActionList();
            QSqlQuery qry(db);
            qry.setForwardOnly(true);
            qry.prepare("SELECT u.account_number, u.nickname, u.gender, u.signature, u.avator "
                        "FROM Friends f "
                        "JOIN Users u ON u.account_number = f.friend_id "
                        "WHERE f.user_id = :user_id");
            qry.bindValue(":user_id", account);
            if (!qry.exec()) return WorkerActionList();
            QJsonArray friendsArray;
            while (qry.next()) {
                const QString friendId = qry.value("account_number").toString();
                const QString avatorUrl = qry.value("avator").toString();
                QJsonObject friendJson;
                friendJson["account_number"] = friendId;
                friendJson["nickname"] = qry.value("nickname").toString();
                friendJson["gender"] = qry.value("gender").toString();
                friendJson["signature"] = qry.value("signature").toString();
                friendJson["avator"] = avatorUrl;
                if (needDownloadAccounts.contains(friendId))
                    friendJson["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, avatorUrl);
                friendsArray.append(friendJson);
            }
            QSqlQuery userQuery(db);
            userQuery.setForwardOnly(true);
            userQuery.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
            userQuery.bindValue(":account", account);
            if (!userQuery.exec() || !userQuery.next()) return WorkerActionList();
            QJsonObject accountJson;
            accountJson["account_number"] = userQuery.value("account_number").toString();
            accountJson["nickname"] = userQuery.value("nickname").toString();
            accountJson["gender"] = userQuery.value("gender").toString();
            accountJson["signature"] = userQuery.value("signature").toString();
            QString avatorUrl = userQuery.value("avator").toString();
            accountJson["avator"] = avatorUrl;
            if (needDownloadAccounts.contains(account))
                accountJson["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, avatorUrl);
            friendsArray.append(accountJson);
            // 4) Action 返回
            QJsonObject finalJson;
            finalJson["tag"] = "loginmessage1";
            finalJson["friends"] = friendsArray;
            return ClientHandlerShared::encodeSingleSendSelf(finalJson);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理登录初始化步骤二（完成账号绑定、拉取未读消息）。
void ClientHandler::dealLoginMes2(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr = this;
    // 1) 参数读取
    const QString account = json.value("account").toString();
    // 2) 参数校验
    if (account.isEmpty()) {
        return;
    }
    // 登录阶段先完成账号绑定。
    quint64 accountId = 0;
    if (!tryParseAccountId(account, accountId)) return;
    bindAccountAndKickOld(accountId);

    const QString savePath = m_savePath;
    const QString sessionTok = m_sessionToken;
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath, sessionTok]() -> WorkerActionList {
            // 3) DB 查询
            const QString account = json["account"].toString();
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return WorkerActionList();
            QSqlQuery qry(db);
            qry.setForwardOnly(true);

            QJsonArray friendsRequestsArray;
            qry.prepare("SELECT u.account_number, u.nickname, u.gender, u.signature, u.avator "
                        "FROM FriendRequests fr "
                        "JOIN Users u ON u.account_number = fr.sender_id "
                        "WHERE fr.receiver_id = :receiver_id AND fr.status = :status");
            qry.bindValue(":receiver_id", account);
            qry.bindValue(":status", "pending");
            if (qry.exec()) {
                while (qry.next()) {
                    const QString avatorUrl = qry.value("avator").toString();
                    QJsonObject friendJson;
                    friendJson["account_number"] = qry.value("account_number").toString();
                    friendJson["nickname"] = qry.value("nickname").toString();
                    friendJson["gender"] = qry.value("gender").toString();
                    friendJson["signature"] = qry.value("signature").toString();
                    friendJson["avator"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, avatorUrl);
                    friendsRequestsArray.append(friendJson);
                }
            }

            QJsonArray unreadMessageArray;
            qry.prepare("SELECT message_id, sender_id, receiver_id, content, filename, timestamp, message_type "
                        "FROM Messages WHERE receiver_id = :receiver_id AND status = :status ORDER BY timestamp ASC");
            qry.bindValue(":receiver_id", account);
            qry.bindValue(":status", "unread");
            if (qry.exec()) {
                while (qry.next()) {
                    QJsonObject messageJson;
                    messageJson["uuid"] = ClientHandlerShared::binaryUuidToStringStatic(qry.value("message_id").toByteArray());
                    messageJson["sender"] = qry.value("sender_id").toString();
                    messageJson["receiver"] = qry.value("receiver_id").toString();
                    const QString msgType = qry.value("message_type").toString();
                    messageJson["message_type"] = msgType;
                    messageJson["timestamp"] = qry.value("timestamp").toString();
                    if (msgType == "audio" || msgType == "picture") {
                        const QString filePath = savePath + qry.value("content").toString();
                        QFile myFile(filePath);
                        if (myFile.open(QIODevice::ReadOnly)) {
                            QByteArray fileData = myFile.readAll();
                            myFile.close();
                            if (!fileData.isEmpty()) {
                                messageJson["content"] = QString::fromLatin1(fileData.toBase64().constData());
                            } else {
                                messageJson["content"] = "";
                                messageJson["file_unavailable"] = true;
                                messageJson["file_unavailable_reason"] = "file empty on disk";
                            }
                        } else {
                            myFile.close();
                            messageJson["content"] = "";
                            messageJson["file_unavailable"] = true;
                            messageJson["file_unavailable_reason"] = "file not found or unreadable";
                        }
                    } else if (msgType == "document") {
                        messageJson["content"] = qry.value("filename").toString();
                        messageJson["need_download"] = true;
                    } else {
                        messageJson["content"] = qry.value("content").toString();
                    }
                    unreadMessageArray.append(messageJson);
                }
            }

            // 4) Action 返回
            QJsonObject finalJson;
            finalJson["tag"] = "loginmessage2";
            finalJson["newfriends"] = friendsRequestsArray;
            finalJson["unreadmessages"] = unreadMessageArray;
            // 供客户端调用 chat_ai_analyze 时回传，与连接上 m_sessionToken 一致。
            if (!sessionTok.isEmpty()) {
                finalJson["session_token"] = sessionTok;
            }
            return ClientHandlerShared::encodeSingleSendSelf(finalJson);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// ===== 好友管理 =====
// 处理删除好友（双向删除好友关系）。
void ClientHandler::dealDeleteFriend(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QString friendAccount = json["friend"].toString();
            auto deleteFriendFail = []() -> WorkerActionList {
                QJsonObject payload;
                payload["tag"] = "deletefriendfail";
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            };
            // 2) 参数校验
            if (account.isEmpty() || friendAccount.isEmpty()) {
                return deleteFriendFail();
            }
            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return deleteFriendFail();
            }
            QSqlQuery qry(db);
            QJsonObject selfPayload;
            selfPayload["tag"] = "deletefriendfail";
            if (!qSqlStartTransaction(db)) {
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(selfPayload));
                return ClientHandlerShared::encodeActions(actions);
            }
            qry.prepare("DELETE FROM Friends WHERE (user_id = :account AND friend_id = :friend) OR (user_id = :friend AND friend_id = :account)");
            qry.bindValue(":account", account);
            qry.bindValue(":friend", friendAccount);
            if (!qry.exec()) {
                qSqlRollback(db);
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(selfPayload));
                return ClientHandlerShared::encodeActions(actions);
            }
            if (!qSqlCommit(db)) {
                qSqlRollback(db);
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(selfPayload));
                return ClientHandlerShared::encodeActions(actions);
            }

            // 4) Action 返回
            selfPayload["tag"] = "deletefriendsucceed";
            selfPayload["account"] = friendAccount;
            QJsonObject notify;
            notify["tag"] = "youaredeleted";
            notify["account"] = account;

            QJsonArray actions;
            actions.append(ClientHandlerShared::makeSendSelfAction(selfPayload));
            actions.append(ClientHandlerShared::makeSendOtherAction(friendAccount, notify));
            return ClientHandlerShared::encodeActions(actions);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理搜索账号（按账号查询用户信息）。
void ClientHandler::dealSearchAccount(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            auto searchFail = []() -> WorkerActionList {
                QJsonObject payload;
                payload["tag"] = "serchaccount";
                payload["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            };
            // 2) 参数校验
            if (account.isEmpty()) {
                return searchFail();
            }
            // 3) DB 查询
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return searchFail();
            }
            QSqlQuery qry(db);
            qry.setForwardOnly(true);
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "serchaccount";
            qry.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
            qry.bindValue(":account", account);
            if (!qry.exec()) {
                qjsonObj["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            // 4) Action 返回
            if (qry.next()) {
                qjsonObj["answer"] = "succeed";
                qjsonObj["account_number"] = qry.value("account_number").toString();
                qjsonObj["nickname"] = qry.value("nickname").toString();
                qjsonObj["gender"] = qry.value("gender").toString();
                qjsonObj["signature"] = qry.value("signature").toString();
                qjsonObj["avator"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, qry.value("avator").toString());
            } else {
                qjsonObj["answer"] = "fail";
            }
            return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// ===== 资料与密码 =====
// 处理更新资料（昵称、性别、签名、头像）。
void ClientHandler::dealChangeInformation(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            // 2) 参数校验
            if (account.isEmpty()) {
                QJsonObject r;
                r["tag"] = "changeinformation";
                r["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }

            // 统一失败返回
            auto failChangeInfo = []() -> WorkerActionList {
                QJsonObject r;
                r["tag"] = "changeinformation";
                r["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(r);
            };

            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return failChangeInfo();
            }
            if (!qSqlStartTransaction(db)) {
                return failChangeInfo();
            }

            QString oldNickname;
            QString oldGender;
            QString oldSignature;
            QString oldAvatorFileName;
            {
                QSqlQuery preQry(db);
                preQry.setForwardOnly(true);
                preQry.prepare("SELECT nickname, gender, signature, avator FROM Users WHERE account_number = :account");
                preQry.bindValue(":account", account);
                if (!preQry.exec() || !preQry.next()) {
                    qSqlRollback(db);
                    return failChangeInfo();
                }
                oldNickname = preQry.value(0).toString();
                oldGender = preQry.value(1).toString();
                oldSignature = preQry.value(2).toString();
                oldAvatorFileName = preQry.value(3).toString();
            }

            const QString nickname = json.contains("nickname") ? json.value("nickname").toString() : oldNickname;
            const QString gender = json.contains("gender") ? json.value("gender").toString() : oldGender;
            const QString signature = json.contains("signature") ? json.value("signature").toString() : oldSignature;

            const bool hasAvatarField = json.contains("avator");
            const QString avatorBase64 = hasAvatarField ? json.value("avator").toString() : QString();
            const bool avatarChangedHint = json.value("avatar_changed").toBool() || json.value("avator_changed").toBool();
            const bool shouldTryAvatarUpdate = hasAvatarField && (avatarChangedHint || !avatorBase64.isEmpty());

            QString avatorUrl;
            QString avatorFileName = oldAvatorFileName;
            QString responseAvatarBase64;
            bool avatarUpdated = false;

            if (shouldTryAvatarUpdate && !avatorBase64.isEmpty()) {
                const QByteArray newAvatarData = QByteArray::fromBase64(avatorBase64.toUtf8());
                if (newAvatarData.isEmpty()) {
                    qSqlRollback(db);
                    return failChangeInfo();
                }

                bool avatarSame = false;
                if (!oldAvatorFileName.isEmpty()) {
                    const QString oldAvatarPath = savePath + "ava/" + oldAvatorFileName;
                    QFile oldAvatarFile(oldAvatarPath);
                    if (oldAvatarFile.open(QIODevice::ReadOnly)) {
                        const QByteArray oldAvatarData = oldAvatarFile.readAll();
                        oldAvatarFile.close();
                        avatarSame = (QCryptographicHash::hash(oldAvatarData, QCryptographicHash::Sha256) ==
                                      QCryptographicHash::hash(newAvatarData, QCryptographicHash::Sha256));
                    }
                }

                if (!avatarSame) {
                    const qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
                    avatorFileName = QString("%1_%2.png").arg(account).arg(timestamp);
                    const QString avaSubDir = savePath + "ava/";
                    QDir().mkpath(avaSubDir);
                    const QString avatorFullPath = avaSubDir + avatorFileName;
                    avatorUrl = "ava/" + avatorFileName;
                    QFile avatorFile(avatorFullPath);
                    if (!avatorFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || avatorFile.write(newAvatarData) != newAvatarData.size()) {
                        avatorFile.close();
                        QFile::remove(avatorFullPath);
                        qSqlRollback(db);
                        return failChangeInfo();
                    }
                    avatorFile.close();
                    avatarUpdated = true;
                    responseAvatarBase64 = avatorBase64;
                }
            }

            // 4) Action 返回
            QSqlQuery qry(db);
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "changeinformation";
            qry.prepare("UPDATE Users SET nickname = :nickname, gender = :gender, signature = :signature, avator = :avator WHERE account_number = :account;");
            qry.bindValue(":nickname", nickname);
            qry.bindValue(":gender", gender);
            qry.bindValue(":signature", signature);
            qry.bindValue(":avator", avatorFileName);
            qry.bindValue(":account", account);
            if (!qry.exec()) {
                qSqlRollback(db);
                if (!avatorUrl.isEmpty()) QFile::remove(savePath + avatorUrl);
                qjsonObj["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            if (!qSqlCommit(db)) {
                qSqlRollback(db);
                if (!avatorUrl.isEmpty()) QFile::remove(savePath + avatorUrl);
                qjsonObj["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            qjsonObj["answer"] = "succeed";
            qjsonObj["nickname"] = nickname;
            qjsonObj["gender"] = gender;
            qjsonObj["signature"] = signature;
            qjsonObj["avator"] = avatorFileName;
            qjsonObj["avatar_updated"] = avatarUpdated;
            if (avatarUpdated) {
                qjsonObj["avatorbase64"] = responseAvatarBase64;
            }
            qjsonObj["account"] = account;
            QJsonArray actions;
            actions.append(ClientHandlerShared::makeSendSelfAction(qjsonObj));
            if (qjsonObj.value("answer").toString() == "succeed") {
                QJsonObject notify;
                notify["tag"] = "changeinfor";
                notify["account"] = qjsonObj.value("account").toString();
                notify["nickname"] = qjsonObj.value("nickname").toString();
                notify["gender"] = qjsonObj.value("gender").toString();
                notify["signature"] = qjsonObj.value("signature").toString();
                notify["avator"] = qjsonObj.value("avator").toString();
                const bool avatarUpdatedOut = qjsonObj.value("avatar_updated").toBool();
                notify["avatar_updated"] = avatarUpdatedOut;
                if (avatarUpdatedOut) {
                    notify["avatorbase64"] = qjsonObj.value("avatorbase64").toString();
                }
                const QJsonArray friendIdsArray = json.value("friendIds").toArray();
                for (const QJsonValue &v : friendIdsArray) {
                    if (v.isString() && !v.toString().isEmpty()) {
                        actions.append(ClientHandlerShared::makeSendOtherAction(v.toString(), notify));
                    }
                }
            }
            return ClientHandlerShared::encodeActions(actions);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理修改密码（需提供旧密码校验）。
void ClientHandler::dealChangePassword(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QString password = json["password"].toString();
            // 2) 参数校验
            if (account.isEmpty() || password.isEmpty()) {
                return ClientHandlerShared::makeAnswerAction("changepassword1", "fail");
            }
            // 3) DB 查询
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return ClientHandlerShared::makeAnswerAction("changepassword1", "fail");
            }
            QSqlQuery qry(db);
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "changepassword1";
            qry.prepare("SELECT password_salt, password FROM Users WHERE account_number = :account");
            qry.bindValue(":account", account);
            if (!qry.exec()) {
                qjsonObj["answer"] = "fail";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            if (!qry.next()) {
                qjsonObj["answer"] = "user_not_found";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            // 4) Action 返回
            QString salt = qry.value(0).toString();
            QString cipherPwd = qry.value(1).toString();
            qjsonObj["answer"] = ClientHandlerShared::verifyWithSaltStatic(password, salt, cipherPwd) ? "succeed" : "wrong_password";
            return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理修改密码二（找回密码流程中的新密码设置）。
void ClientHandler::dealChangePassword2(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QString password = json["password"].toString();
            const QString newPassword = json["newpassword"].toString();
            // 2) 参数校验
            if (account.isEmpty() || password.isEmpty() || newPassword.isEmpty()) {
                QJsonObject failObj;
                failObj["account"] = account;
                failObj["tag"] = "changepassword2";
                failObj["answer"] = "fail";
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(failObj));
                if (!account.isEmpty()) {
                    QJsonObject kick;
                    kick["tag"] = "youarekickedoffline";
                    actions.append(ClientHandlerShared::makeSendOtherAction(account, kick));
                }
                return ClientHandlerShared::encodeActions(actions);
            }

            auto buildResult = [&account](const QString &answer) -> WorkerActionList {
                QJsonObject qjsonObj;
                qjsonObj["account"] = account;
                qjsonObj["tag"] = "changepassword2";
                qjsonObj["answer"] = answer;
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(qjsonObj));
                if (!account.isEmpty()) {
                    QJsonObject kick;
                    kick["tag"] = "youarekickedoffline";
                    actions.append(ClientHandlerShared::makeSendOtherAction(account, kick));
                }
                return ClientHandlerShared::encodeActions(actions);
            };

            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return buildResult("fail");
            }
            QSqlQuery qry(db);
            if (!qSqlStartTransaction(db)) {
                return buildResult("fail");
            }
            qry.prepare("SELECT password_salt, password FROM Users WHERE account_number = :account");
            qry.bindValue(":account", account);
            if (!qry.exec() || !qry.next()) {
                qSqlRollback(db);
                return buildResult("fail");
            }
            if (!ClientHandlerShared::verifyWithSaltStatic(password, qry.value(0).toString(), qry.value(1).toString())) {
                qSqlRollback(db);
                return buildResult("fail");
            }
            QString newPwdSalt = ClientHandlerShared::generateSaltStatic();
            QString newCipherPwd = ClientHandlerShared::encryptWithSaltStatic(newPassword, newPwdSalt);
            qry.prepare("UPDATE Users SET password = :password, password_salt = :password_salt WHERE account_number = :account");
            qry.bindValue(":account", account);
            qry.bindValue(":password", newCipherPwd);
            qry.bindValue(":password_salt", newPwdSalt);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                return buildResult("fail");
            }
            // 4) Action 返回
            return buildResult("succeed");
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理注销（移除在线状态、清理会话）。
void ClientHandler::dealLogout(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QString password = json["password"].toString();

            auto buildLogoutResult = [&account](const QString &answer, const QString &reason = QString()) -> WorkerActionList {
                QJsonObject qjsonObj;
                qjsonObj["tag"] = "logout";
                qjsonObj["account"] = account;
                qjsonObj["answer"] = answer;
                if (!reason.isEmpty()) qjsonObj["reason"] = reason;
                QJsonArray actions;
                actions.append(ClientHandlerShared::makeSendSelfAction(qjsonObj));
                if (!account.isEmpty()) {
                    QJsonObject kick;
                    kick["tag"] = "youarekickedoffline";
                    actions.append(ClientHandlerShared::makeSendOtherAction(account, kick));
                }
                return ClientHandlerShared::encodeActions(actions);
            };

            // 2) 参数校验
            if (account.isEmpty() || password.isEmpty()) {
                return buildLogoutResult("fail", "invalid_params");
            }

            // 3) DB 事务（死锁重试）
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return buildLogoutResult("fail", "db_unavailable");
            }
            const auto isDeadlockError = [](const QSqlError &err) -> bool {
                const QString t = err.text().toLower();
                return t.contains(QStringLiteral("deadlock")) ||
                       t.contains(QStringLiteral("lock wait timeout"));
            };

            QString avatorFileName;
            constexpr int kMaxAttempts = 3;
            for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
                if (!qSqlStartTransaction(db)) {
                    return buildLogoutResult("fail", "transaction_failed");
                }

                QSqlQuery qry(db);
                qry.prepare("SELECT password_salt, password, avator FROM Users WHERE account_number = :account");
                qry.bindValue(":account", account);
                if (!qry.exec() || !qry.next()) {
                    qSqlRollback(db);
                    return buildLogoutResult("fail", "authentication_failed");
                }
                avatorFileName = qry.value(2).toString();
                if (!ClientHandlerShared::verifyWithSaltStatic(password, qry.value(0).toString(), qry.value(1).toString())) {
                    qSqlRollback(db);
                    return buildLogoutResult("fail", "authentication_failed");
                }

                // 与校验 SELECT 分用独立 QSqlQuery，避免 QMYSQL 在复用同一 QSqlQuery 时偶发 DELETE exec 失败
                QSqlQuery delQry(db);
                delQry.prepare("DELETE FROM Users WHERE account_number = :account");
                delQry.bindValue(":account", account);
                if (!delQry.exec()) {
                    const QSqlError err = delQry.lastError();
                    qWarning() << "dealLogout DELETE exec failed account=" << account
                               << "attempt=" << attempt
                               << "error=" << err.text();
                    qSqlRollback(db);
                    if (isDeadlockError(err) && attempt < kMaxAttempts) {
                        continue;
                    }
                    return buildLogoutResult("fail", "delete_failed");
                }
                {
                    const int nr = delQry.numRowsAffected();
                    if (nr == 0) {
                        qSqlRollback(db);
                        return buildLogoutResult("fail", "user_not_found");
                    }
                    // 部分 Qt SQL 驱动对 DELETE 会返回 numRowsAffected==-1；若与 0 一并判失败会误回滚。
                    // MySQL(QMYSQL) 通常返回 1；此分支在 MySQL 上多半不触发，保留无害。
                    if (nr < 0) {
                        QSqlQuery chk(db);
                        chk.prepare("SELECT 1 FROM Users WHERE account_number = :account LIMIT 1");
                        chk.bindValue(":account", account);
                        if (!chk.exec()) {
                            qWarning() << "dealLogout post-delete chk exec failed account=" << account
                                       << "error=" << chk.lastError().text();
                            qSqlRollback(db);
                            return buildLogoutResult("fail", "delete_failed");
                        }
                        if (chk.next()) {
                            qSqlRollback(db);
                            return buildLogoutResult("fail", "user_not_found");
                        }
                    }
                }
                if (!qSqlCommit(db)) {
                    const QSqlError err = db.lastError();
                    qSqlRollback(db);
                    if (isDeadlockError(err) && attempt < kMaxAttempts) {
                        qWarning() << "dealLogout commit deadlock account=" << account
                                   << "attempt=" << attempt
                                   << "error=" << err.text();
                        continue;
                    }
                    return buildLogoutResult("fail", "commit_failed");
                }
                if (!avatorFileName.isEmpty()) {
                    const QString avaPath = savePath + "ava/" + avatorFileName;
                    if (QFile::exists(avaPath)) QFile::remove(avaPath);
                }
                // 4) Action 返回
                return buildLogoutResult("success");
            }
            return buildLogoutResult("fail", "delete_failed");
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理添加好友（发送好友申请）。
void ClientHandler::dealAddFriends(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QString friendAccount = json["friend"].toString();
            // 2) 参数校验
            if (account.isEmpty() || friendAccount.isEmpty() || account == friendAccount) {
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "invalid_params";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "db_unavailable";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            QSqlQuery qry(db);
            qry.prepare("SELECT COUNT(*) FROM FriendRequests WHERE sender_id = :sender AND receiver_id = :receiver AND status = :status");
            qry.bindValue(":sender", account);
            qry.bindValue(":receiver", friendAccount);
            qry.bindValue(":status", "pending");
            if (!qry.exec() || !qry.next()) {
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "fail";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            if (qry.value(0).toInt() > 0) {
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "duplicate";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            if (!qSqlStartTransaction(db)) {
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "fail";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            qry.prepare("INSERT INTO FriendRequests(sender_id, receiver_id, request_type) VALUES(:sender, :receiver, :request_type)");
            qry.bindValue(":sender", account);
            qry.bindValue(":receiver", friendAccount);
            qry.bindValue(":request_type", "friend");
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                QJsonObject r;
                r["tag"] = "addfriend_answer";
                r["answer"] = "fail";
                r["friend"] = friendAccount;
                return ClientHandlerShared::encodeSingleSendSelf(r);
            }
            // 4) Action 返回
            QJsonArray actions;
            QJsonObject r;
            r["tag"] = "addfriend_answer";
            r["answer"] = "ok";
            r["friend"] = friendAccount;
            actions.append(ClientHandlerShared::makeSendSelfAction(r));
            actions.append(ClientHandlerShared::makeSendOtherAction(friendAccount, json)); // 插入成功后转发
            return ClientHandlerShared::encodeActions(actions);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理添加好友回应（同意或拒绝申请）。
void ClientHandler::dealAddFriendsRespond(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString answer = json["answer"].toString();
            const QString sender = json["sender"].toString();
            const QString account = json["account"].toString();

            // 2) 参数校验
            if (sender.isEmpty() || account.isEmpty()) return WorkerActionList();

            auto singleSelf = [&sender](const QString &type, const QString &answerText) -> WorkerActionList {
                QJsonObject payload;
                payload["tag"] = "updatefriendship";
                payload["sender"] = sender;
                payload["type"] = type;
                payload["answer"] = answerText;
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            };

            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return singleSelf("unknown", "fail");
            QSqlQuery qry(db);
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "updatefriendship";
            qjsonObj["sender"] = sender;
            if (answer == "reject") {
                qjsonObj["type"] = "reject";
                qry.prepare("UPDATE FriendRequests SET status = 'rejected' WHERE sender_id = :sender_id AND receiver_id = :receiver_id");
                qry.bindValue(":sender_id", sender);
                qry.bindValue(":receiver_id", account);
                qjsonObj["answer"] = qry.exec() ? "true" : "fail";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            if (answer != "accept") return WorkerActionList();
            qjsonObj["type"] = "accept";
            if (!qSqlStartTransaction(db)) {
                return singleSelf("accept", "fail");
            }
            qry.prepare("UPDATE FriendRequests SET status = 'accepted' WHERE sender_id = :sender_id AND receiver_id = :receiver_id");
            qry.bindValue(":sender_id", sender);
            qry.bindValue(":receiver_id", account);
            if (!qry.exec()) {
                qSqlRollback(db);
                return singleSelf("accept", "fail");
            }
            qry.prepare("SELECT COUNT(*) FROM Friends WHERE user_id = :sender_id");
            qry.bindValue(":sender_id", sender);
            if (!qry.exec() || !qry.next()) {
                qSqlRollback(db);
                return singleSelf("accept", "fail");
            }
            if (qry.value(0).toInt() >= 500) {
                qSqlRollback(db);
                return singleSelf("accept", "friend_limit_exceeded");
            }
            qry.prepare("SELECT COUNT(*) FROM Friends WHERE (user_id = :user_id AND friend_id = :friend_id) OR (user_id = :friend_id AND friend_id = :user_id)");
            qry.bindValue(":user_id", account);
            qry.bindValue(":friend_id", sender);
            if (!qry.exec() || !qry.next()) {
                qSqlRollback(db);
                return singleSelf("accept", "fail");
            }
            if (qry.value(0).toInt() > 0) {
                qSqlRollback(db);
                return singleSelf("accept", "friendship_exists");
            }
            qry.prepare("INSERT INTO Friends(user_id, friend_id) VALUES(:user_id, :friend_id)");
            qry.bindValue(":user_id", account);
            qry.bindValue(":friend_id", sender);
            if (!qry.exec()) {
                qSqlRollback(db);
                return singleSelf("accept", "fail");
            }
            qry.prepare("INSERT INTO Friends(user_id, friend_id) VALUES(:friend_id, :user_id)");
            qry.bindValue(":friend_id", sender);
            qry.bindValue(":user_id", account);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                return singleSelf("accept", "fail");
            }
            QSqlQuery userQuery(db);
            userQuery.setForwardOnly(true);
            userQuery.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
            userQuery.bindValue(":account", sender);
            if (userQuery.exec() && userQuery.next()) {
                qjsonObj["account_number"] = userQuery.value("account_number").toString();
                qjsonObj["nickname"] = userQuery.value("nickname").toString();
                qjsonObj["gender"] = userQuery.value("gender").toString();
                qjsonObj["signature"] = userQuery.value("signature").toString();
                qjsonObj["avator"] = userQuery.value("avator").toString();
                qjsonObj["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, userQuery.value("avator").toString());
            }
            // 4) Action 返回
            qjsonObj["answer"] = "succeed";
            QJsonArray actions;
            actions.append(ClientHandlerShared::makeSendSelfAction(qjsonObj));
            QJsonObject pass;
            pass["tag"] = "requestpass";
            pass["account"] = account;
            actions.append(ClientHandlerShared::makeSendOtherAction(sender, pass));
            return ClientHandlerShared::encodeActions(actions);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// ===== 消息 =====
// 处理聊天消息（入库并转发给接收方）。
void ClientHandler::dealMessages(const QJsonObject& json)
{
    QPointer<ClientHandler> thisPtr(this);
    // 1) 参数读取
    const QString ackUuid = json.value("uuid").toString();
    // 2) 提前 ACK（连接线程）
    if (!ackUuid.trimmed().isEmpty() && !QUuid::fromString(ackUuid).isNull()) {
        ClientHandlerShared::runOnObjectThread(this, [thisPtr, ackUuid]() {
            if (thisPtr.isNull()) return;
            thisPtr->executeWorkerAction(ClientHandlerShared::makeAckAction(ackUuid));
        });
    }

    // 3) DB 任务
    const QString serverTimestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QJsonObject jsonCopy = json;
    jsonCopy["timestamp"] = serverTimestamp;
    QPointer<ClientHandler> handlerPtr(this);

    ClientHandlerShared::runDbTask(
        this,
        [jsonCopy, serverTimestamp, savePath = m_savePath, handlerPtr]() -> WorkerActionList {
            const QString sender = jsonCopy["sender"].toString();
            const QString receiver = jsonCopy["receiver"].toString();
            const QString messagetype = jsonCopy["messagetype"].toString();
            const QString uuidStr = jsonCopy["uuid"].toString();
            const QString messages = jsonCopy["messages"].toString();
            const QString filename = jsonCopy["filename"].toString();
            QSqlDatabase db = ConnectionPool::getInstance().getConnection(15000);
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                qWarning() << "【连接处理】消息任务执行失败: db无效或未连接 sender=" << sender << "receiver=" << receiver;
                return WorkerActionList();
            }
            QSqlQuery checkQry(db);
            checkQry.prepare("SELECT COUNT(*) FROM Friends WHERE (user_id = :u1 AND friend_id = :f1) OR (user_id = :u2 AND friend_id = :f2)");
            checkQry.bindValue(":u1", sender);
            checkQry.bindValue(":f1", receiver);
            checkQry.bindValue(":u2", receiver);
            checkQry.bindValue(":f2", sender);
            if (!checkQry.exec() || !checkQry.next() || checkQry.value(0).toInt() == 0) {
                qWarning() << "【连接处理】消息任务执行失败: 非好友关系 sender=" << sender << "receiver=" << receiver;
                return WorkerActionList();
            }
            if (messagetype == "document") {
                if (!handlerPtr.isNull()) {
                    ClientHandlerShared::runOnObjectThread(handlerPtr.data(), [handlerPtr, jsonCopy]() {
                        if (handlerPtr.isNull()) return;
                        {
                            QMutexLocker locker(&handlerPtr->m_queMutex);
                            handlerPtr->m_messageQueue.enqueue(PendingMessageItem{jsonCopy, 0});
                        }
                        if (!handlerPtr->m_isSending) {
                            handlerPtr->dealNextMessage();
                        }
                    });
                }
                return ClientHandlerShared::encodeActions(QJsonArray());
            }
            QUuid uuid = QUuid::fromString(uuidStr);
            if (uuid.isNull()) {
                qWarning() << "【连接处理】消息任务执行失败: uuid无效 uuid=" << uuidStr;
                return WorkerActionList();
            }
            QByteArray messageIdBin = uuid.toRfc4122();
            if (!qSqlStartTransaction(db)) {
                qWarning() << "【连接处理】消息任务执行失败: 事务启动失败 sender=" << sender << "receiver=" << receiver;
                return WorkerActionList();
            }
            QSqlQuery qry(db);
            QString contentToStore;
            QString fileUrl;
            if (messagetype == "audio" || messagetype == "picture") {
                QString subDir = savePath + messagetype + "/";
                QDir().mkpath(subDir);
                QString ext = (messagetype == "picture") ? "png" : "audio";
                QString uniqueFileName = uuidStr + "." + ext;
                fileUrl = messagetype + "/" + uniqueFileName;
                QFile file(subDir + uniqueFileName);
                if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(messages.toUtf8())) < 0) {
                    file.close();
                    qSqlRollback(db);
                    qWarning() << "【连接处理】消息任务执行失败: 文件写入失败 messagetype=" << messagetype << "uuid=" << uuidStr;
                    return WorkerActionList();
                }
                file.close();
                contentToStore = fileUrl;
            } else {
                contentToStore = messages;
            }
            qry.prepare("INSERT INTO Messages (message_id, sender_id, receiver_id, content, message_type, status, timestamp, filename) "
                        "VALUES (:message_id, :sender, :receiver, :content, :messagetype, :status, :timestamp, :filename)");
            qry.bindValue(":message_id", messageIdBin);
            qry.bindValue(":sender", sender);
            qry.bindValue(":receiver", receiver);
            qry.bindValue(":content", contentToStore);
            qry.bindValue(":messagetype", messagetype);
            qry.bindValue(":status", "unread");
            qry.bindValue(":timestamp", serverTimestamp);
            qry.bindValue(":filename", filename);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                if (!fileUrl.isEmpty()) QFile::remove(savePath + fileUrl);
                qWarning() << "【连接处理】消息任务执行失败: INSERT/commit失败 sender=" << sender << "receiver=" << receiver
                           << "qryError=" << qry.lastError().text();
                return WorkerActionList();
            }
            // 4) Action 返回
            QJsonObject responseJson;
            responseJson["tag"] = "yourmessages";
            responseJson["sender"] = sender;
            responseJson["messagetype"] = messagetype;
            responseJson["receiver"] = receiver;
            responseJson["timestamp"] = serverTimestamp;
            responseJson["filename"] = filename;
            responseJson["messages"] = messages;
            responseJson["uuid"] = uuidStr;
            return ClientHandlerShared::encodeSingleSendOther(receiver, responseJson);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            if (result.isEmpty()) {
                qWarning() << "【连接处理】消息任务执行失败";
                return;
            }
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理下一条文档消息（从队列取一条并发送）。
void ClientHandler::dealNextMessage()
{
    // 1) 从队列取任务
    PendingMessageItem pending;
    {
        QMutexLocker locker(&m_queMutex);
        if (m_messageQueue.isEmpty()) {
            m_isSending = false;
            return;
        }
        pending = m_messageQueue.dequeue();
    }
    m_isSending = true;
    const QJsonObject json = pending.json;

    QPointer<ClientHandler> thisPtr(this);

    auto resultHolder = QSharedPointer<WorkerActionList>::create();
    auto taskDone = QSharedPointer<bool>::create(false);
    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,
        [json, savePath = m_savePath, resultHolder, taskDone]() {
            // 2) 参数校验与文件写入 + DB 持久化
            QString uuidStr = json["uuid"].toString();
            if (uuidStr.isEmpty()) {
                *taskDone = true;
                return;
            }
            QUuid uuid = QUuid::fromString(uuidStr);
            if (uuid.isNull()) {
                *taskDone = true;
                return;
            }
            QByteArray messageIdBin = uuid.toRfc4122();
            QString fileType = "document";
            QString subDir = savePath + fileType + "/";
            QDir dir(subDir);
            if (!dir.exists() && !dir.mkpath(".")) {
                *taskDone = true;
                return;
            }
            QString originalFileName = json["filename"].toString();
            QString fileSuffix = originalFileName.split(".").last();
            QString uniqueFileName = uuidStr;
            if (!fileSuffix.isEmpty()) uniqueFileName += "." + fileSuffix;
            QString filePath = subDir + uniqueFileName;
            QString fileUrl = fileType + "/" + uniqueFileName;
            QByteArray fileData = QByteArray::fromBase64(json["messages"].toString().toUtf8());
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly) || file.write(fileData) == -1) {
                file.close();
                QFile::remove(filePath);
                *taskDone = true;
                return;
            }
            file.close();
            QSqlDatabase db = ConnectionPool::getInstance().getConnection(3000);
            if (!db.isValid() || !db.isOpen()) {
                QFile::remove(filePath);
                *taskDone = true;
                return;
            }
            DbConnectionGuard guard(db);
            if (!qSqlStartTransaction(db)) {
                QFile::remove(filePath);
                *taskDone = true;
                return;
            }
            QSqlQuery qry(db);
            qry.prepare("INSERT INTO Messages (message_id, sender_id, receiver_id, content, message_type, status, timestamp, filename) "
                        "VALUES (:message_id, :sender, :receiver, :content, :messagetype, :status, :timestamp, :filename)");
            qry.bindValue(":message_id", messageIdBin);
            qry.bindValue(":sender", json["sender"].toVariant());
            qry.bindValue(":receiver", json["receiver"].toVariant());
            qry.bindValue(":content", fileUrl);
            qry.bindValue(":messagetype", json["messagetype"].toVariant());
            qry.bindValue(":status", "unread");
            qry.bindValue(":timestamp", json["timestamp"].toVariant());
            qry.bindValue(":filename", originalFileName);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                QFile::remove(filePath);
                *taskDone = true;
                return;
            }
            // 3) Action 返回
            QJsonObject responseJson;
            responseJson["tag"] = "yourmessages";
            responseJson["sender"] = json["sender"];
            responseJson["messagetype"] = json["messagetype"];
            responseJson["receiver"] = json["receiver"];
            responseJson["timestamp"] = json["timestamp"];
            responseJson["filename"] = originalFileName;
            responseJson["messages"] = originalFileName;
            responseJson["uuid"] = json["uuid"];
            QJsonArray actions;
            QJsonObject done;
            done["tag"] = "uploaddone";
            done["messagetype"] = json["messagetype"];
            done["uuid"] = json["uuid"];
            actions.append(ClientHandlerShared::makeSendSelfAction(done));
            const QString receiverId = json["receiver"].toString();
            if (!receiverId.isEmpty()) {
                actions.append(ClientHandlerShared::makeSendOtherAction(receiverId, responseJson));
            }
            *resultHolder = ClientHandlerShared::encodeActions(actions);
            *taskDone = true;
        },
        [thisPtr, pending, resultHolder, taskDone]() {
            if (thisPtr.isNull()) return;
            // 4) 回收与重试控制
            if (!*taskDone) {
                qWarning() << "【连接处理】文档消息任务未完成";
            }
            const WorkerActionList &result = *resultHolder;
            if (result.isEmpty()) {
                if (pending.retryCount + 1 > thisPtr->m_messageMaxRetries) {
                    qWarning() << "【连接处理】文档消息重试超过上限，丢弃 uuid="
                               << pending.json.value("uuid").toString();
                    thisPtr->m_isSending = false;
                    thisPtr->dealNextMessage();
                    return;
                }
                QMutexLocker locker(&thisPtr->m_queMutex);
                PendingMessageItem retryItem = pending;
                retryItem.retryCount += 1;
                thisPtr->m_messageQueue.prepend(retryItem);
                thisPtr->m_isSending = false;
                return;
            }
            thisPtr->consumeWorkerActions(result);
            thisPtr->dealNextMessage();
        });
}

// 处理下载文件请求（分片读取并发送文档内容）。
void ClientHandler::dealAskDocument(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    // 1) 参数读取
    const int chunkBytes = ServerConfigDefaults::serverDocChunkBytes();
    const QString uuidStr = json["uuid"].toString();
    const QString requester = json.value("account").toString();
    qInfo() << "【文档下载】收到请求 uuid=" << uuidStr << " requester=" << requester;

    ClientHandlerShared::runDbTask(
        this,
        [json, chunkBytes, thisPtr]() -> QByteArray {
            // 2) 参数校验
            const QString requester = json.value("account").toString();
            const QString uuidStr = json["uuid"].toString();
            if (uuidStr.isEmpty()) {
                qWarning() << "【文档下载】失败 uuid 为空";
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "uuid missing";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            QUuid uuid = QUuid::fromString(uuidStr);
            if (uuid.isNull()) {
                // 兼容不带大括号的 UUID 字符串
                uuid = QUuid::fromString("{" + uuidStr + "}");
            }
            if (uuid.isNull()) {
                qWarning() << "【文档下载】失败 uuid 格式无效 uuidStr=" << uuidStr;
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "invalid uuid";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            const QByteArray messageIdBin = uuid.toRfc4122();
            const QString basePath = (thisPtr && !thisPtr->m_savePath.isEmpty())
                ? thisPtr->m_savePath
                : ServerConfigDefaults::storageBasePath();
            // 3) DB 查询 + 文件分片读取
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                qWarning() << "【文档下载】失败 数据库不可用";
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "database unavailable";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            QSqlQuery qry(db);
            qry.prepare("SELECT sender_id, receiver_id, content, filename FROM Messages WHERE message_id = :message_id");
            qry.bindValue(":message_id", messageIdBin);
            if (!qry.exec() || !qry.next()) {
                qWarning() << "【文档下载】失败 DB 中无记录 uuid=" << uuidStr;
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "File record not found in database";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            const QString senderId = qry.value(0).toString();
            const QString receiverId = qry.value(1).toString();
            if (requester.isEmpty() || (requester != senderId && requester != receiverId)) {
                qWarning() << "【文档下载】失败 权限不足 requester=" << requester
                           << " sender=" << senderId << " receiver=" << receiverId;
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "permission denied";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            const QString fileUrl = qry.value(2).toString();
            const QString originalFileName = qry.value(3).toString();
            const QString filePath = basePath + fileUrl;
            qInfo() << "【文档下载】DB 找到记录 fileUrl=" << fileUrl << " filePath=" << filePath;

            const QFileInfo fileInfo(filePath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                qWarning() << "【文档下载】失败 文件不存在 path=" << filePath;
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file not found on disk";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            const qint64 totalBytes = fileInfo.size();
            if (totalBytes <= 0) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file empty on disk";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            if (thisPtr.isNull()) {
                return QByteArray();
            }

            qInfo() << "【文档下载】校验通过，准备流式发送 filePath=" << filePath
                    << " totalBytes=" << totalBytes;
            return makeDocDownloadReadyPayload(uuidStr, filePath, originalFileName, totalBytes, chunkBytes);
        },
        [thisPtr, json](const QByteArray &result) {
            if (thisPtr.isNull()) return;
            thisPtr->handleDocDownloadDbResult(result, json["uuid"].toString());
        });
}

// 处理网盘文件下载请求（任意登录用户凭 file_id 下载）。
void ClientHandler::dealAskCloudFile(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    const int chunkBytes = ServerConfigDefaults::serverDocChunkBytes();
    const QString uuidStr = json.value("file_id").toString(json.value("uuid").toString());
    const QString requester = json.value("account").toString();
    qInfo() << "【网盘下载】收到请求 file_id=" << uuidStr << " requester=" << requester;

    ClientHandlerShared::runDbTask(
        this,
        [json, chunkBytes, thisPtr]() -> QByteArray {
            const QString requester = json.value("account").toString();
            const QString uuidStr = json.value("file_id").toString(json.value("uuid").toString());
            if (uuidStr.isEmpty()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file_id missing";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            if (requester.isEmpty()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "account missing";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            QUuid uuid = QUuid::fromString(uuidStr);
            if (uuid.isNull()) {
                uuid = QUuid::fromString("{" + uuidStr + "}");
            }
            if (uuid.isNull()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "invalid file_id";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }
            const QByteArray fileIdBin = uuid.toRfc4122();
            const QString basePath = (thisPtr && !thisPtr->m_savePath.isEmpty())
                ? thisPtr->m_savePath
                : ServerConfigDefaults::storageBasePath();

            QSqlDatabase db = ConnectionPool::getInstance().getConnection(15000);
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "database unavailable";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            QSqlQuery userQry(db);
            userQry.prepare("SELECT account_number FROM Users WHERE account_number = :account");
            userQry.bindValue(":account", requester);
            if (!userQry.exec() || !userQry.next()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "invalid requester";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            QSqlQuery qry(db);
            qry.prepare("SELECT owner_id, content, filename FROM CloudFiles WHERE file_id = :file_id");
            qry.bindValue(":file_id", fileIdBin);
            if (!qry.exec() || !qry.next()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file not found";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            const QString fileUrl = qry.value(1).toString();
            const QString originalFileName = qry.value(2).toString();
            const QString filePath = basePath + fileUrl;
            qInfo() << "【网盘下载】DB 找到记录 fileUrl=" << fileUrl << " filePath=" << filePath;

            const QFileInfo fileInfo(filePath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file not found on disk";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            const qint64 totalBytes = fileInfo.size();
            if (totalBytes <= 0) {
                QJsonObject err;
                err["tag"] = "document_error";
                err["uuid"] = uuidStr;
                err["message"] = "file empty on disk";
                return ClientHandlerShared::toJsonCompactBytes(err);
            }

            if (thisPtr.isNull()) {
                return QByteArray();
            }

            qInfo() << "【网盘下载】校验通过，准备流式发送 filePath=" << filePath
                    << " totalBytes=" << totalBytes;
            return makeDocDownloadReadyPayload(uuidStr, filePath, originalFileName, totalBytes, chunkBytes);
        },
        [thisPtr, json](const QByteArray &result) {
            if (thisPtr.isNull()) return;
            const QString uuidStr = json.value("file_id").toString(json.value("uuid").toString());
            thisPtr->handleDocDownloadDbResult(result, uuidStr);
        });
}

// 查询网盘文件信息。
void ClientHandler::dealSearchCloudFile(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            const QString requester = json.value("account").toString();
            const QString fileIdStr = json.value("file_id").toString();
            auto makeResult = [](bool found, const QJsonObject &extra = QJsonObject()) -> WorkerActionList {
                QJsonObject payload;
                payload["tag"] = "searchcloudfile_result";
                payload["found"] = found ? QStringLiteral("true") : QStringLiteral("false");
                for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
                    payload.insert(it.key(), it.value());
                }
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            };

            if (requester.isEmpty() || fileIdStr.isEmpty()) {
                return makeResult(false);
            }

            QUuid uuid = QUuid::fromString(fileIdStr);
            if (uuid.isNull()) {
                uuid = QUuid::fromString("{" + fileIdStr + "}");
            }
            if (uuid.isNull()) {
                return makeResult(false);
            }

            QSqlDatabase db = ConnectionPool::getInstance().getConnection(15000);
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return makeResult(false);
            }

            QSqlQuery userQry(db);
            userQry.prepare("SELECT account_number FROM Users WHERE account_number = :account");
            userQry.bindValue(":account", requester);
            if (!userQry.exec() || !userQry.next()) {
                return makeResult(false);
            }

            QSqlQuery qry(db);
            qry.prepare("SELECT owner_id, filename, file_size, timestamp FROM CloudFiles WHERE file_id = :file_id");
            qry.bindValue(":file_id", uuid.toRfc4122());
            if (!qry.exec() || !qry.next()) {
                return makeResult(false);
            }

            QJsonObject extra;
            extra["file_id"] = fileIdStr;
            extra["filename"] = qry.value(1).toString();
            extra["owner"] = qry.value(0).toString();
            extra["file_size"] = qry.value(2).toString();
            extra["timestamp"] = qry.value(3).toString();

            return makeResult(true, extra);
        },
        [thisPtr](const WorkerActionList &actions) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(actions);
        });
}

// 列出当前用户上传的网盘文件。
void ClientHandler::dealListMyCloudFiles(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> WorkerActionList {
            const QString requester = json.value("account").toString();
            QJsonObject payload;
            payload["tag"] = "listmycloudfiles_result";
            payload["ok"] = QStringLiteral("false");

            if (requester.isEmpty()) {
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            }

            QSqlDatabase db = ConnectionPool::getInstance().getConnection(15000);
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            }

            QSqlQuery userQry(db);
            userQry.prepare("SELECT account_number FROM Users WHERE account_number = :account");
            userQry.bindValue(":account", requester);
            if (!userQry.exec() || !userQry.next()) {
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            }

            QSqlQuery qry(db);
            qry.prepare("SELECT file_id, filename, file_size, timestamp FROM CloudFiles "
                        "WHERE owner_id = :owner ORDER BY timestamp DESC, file_id DESC LIMIT 100");
            qry.bindValue(":owner", requester);
            if (!qry.exec()) {
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            }

            QJsonArray files;
            while (qry.next()) {
                const QByteArray fileIdBin = qry.value(0).toByteArray();
                QUuid fileUuid = QUuid::fromRfc4122(fileIdBin);
                if (fileUuid.isNull()) {
                    continue;
                }
                QJsonObject item;
                item["file_id"] = fileUuid.toString(QUuid::WithoutBraces);
                item["filename"] = qry.value(1).toString();
                item["file_size"] = qry.value(2).toString();
                item["timestamp"] = qry.value(3).toString();
                files.append(item);
            }

            payload["ok"] = QStringLiteral("true");
            payload["files"] = files;
            return ClientHandlerShared::encodeSingleSendSelf(payload);
        },
        [thisPtr](const WorkerActionList &actions) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(actions);
        });
}

// 处理索要好友信息（返回指定账号的资料）。
void ClientHandler::dealAskForFriend(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json, savePath = m_savePath]() -> WorkerActionList {
            // 1) 参数读取
            const QString reqUserAccount = json["account"].toString();
            const QString targetFriendAccount = json["friend"].toString();
            // 2) 参数校验
            if (reqUserAccount.isEmpty() || targetFriendAccount.isEmpty()) return WorkerActionList();
            // 3) DB 查询
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                QJsonObject payload;
                payload["tag"] = "friendinfor";
                payload["error"] = "db_unavailable";
                return ClientHandlerShared::encodeSingleSendSelf(payload);
            }
            QJsonObject qjsonObj;
            qjsonObj["tag"] = "friendinfor";
            QSqlQuery checkQry(db);
            checkQry.prepare("SELECT COUNT(*) FROM Friends WHERE (user_id = :u1 AND friend_id = :f1) OR (user_id = :u2 AND friend_id = :f2)");
            checkQry.bindValue(":u1", reqUserAccount);
            checkQry.bindValue(":f1", targetFriendAccount);
            checkQry.bindValue(":u2", targetFriendAccount);
            checkQry.bindValue(":f2", reqUserAccount);
            if (!checkQry.exec() || !checkQry.next() || checkQry.value(0).toInt() == 0) {
                qjsonObj["error"] = "not_friend_relation";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            QSqlQuery qry(db);
            qry.setForwardOnly(true);
            qry.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
            qry.bindValue(":account", targetFriendAccount);
            if (!qry.exec()) {
                qjsonObj["error"] = "user_query_failed";
                return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
            }
            if (qry.next()) {
                qjsonObj["account_number"] = qry.value("account_number").toString();
                qjsonObj["nickname"] = qry.value("nickname").toString();
                qjsonObj["gender"] = qry.value("gender").toString();
                qjsonObj["signature"] = qry.value("signature").toString();
                qjsonObj["avator"] = ClientHandlerShared::getAvaFromUrlStatic(savePath, qry.value("avator").toString());
            } else {
                qjsonObj["error"] = "user_not_found";
            }
            // 4) Action 返回
            return ClientHandlerShared::encodeSingleSendSelf(qjsonObj);
        },
        [thisPtr](const WorkerActionList &result) {
            if (thisPtr.isNull()) return;
            thisPtr->consumeWorkerActions(result);
        });
}

// 处理消息已读（更新单条消息状态）。
void ClientHandler::dealMessageRead(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> QByteArray {
            // 1) 参数读取 + 2) 参数校验（Messages.receiver_id 存 account_number）
            const QString uuidStr = json["uuid"].toString();
            if (uuidStr.isEmpty()) return QByteArray();
            const QUuid uuid = QUuid::fromString(uuidStr);
            if (uuid.isNull()) return QByteArray();
            const QByteArray messageIdBin = uuid.toRfc4122();
            const QString receiverId = json.value("account").toString();
            if (receiverId.isEmpty()) return QByteArray();
            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return QByteArray();
            if (!qSqlStartTransaction(db)) return QByteArray();
            QSqlQuery qry(db);
            qry.prepare("UPDATE Messages SET status = :new_status WHERE receiver_id = :receiver_id AND message_id = :message_id");
            qry.bindValue(":new_status", "done");
            qry.bindValue(":receiver_id", receiverId);
            qry.bindValue(":message_id", messageIdBin);
            if (!qry.exec() || !qSqlCommit(db)) {
                qSqlRollback(db);
                return QByteArray();
            }
            // 4) 返回执行标记
            return QByteArray("1");
        },
        [thisPtr](const QByteArray &result) {
            if (thisPtr.isNull()) return;
            if (!result.isEmpty()) {
                QJsonObject ack;
                ack["tag"] = "messageread_ack";
                thisPtr->socketWrite(ClientHandlerShared::toJsonCompactBytes(ack));
            }
        });
}

// 处理登录消息已读（批量更新消息已读状态）。
void ClientHandler::dealLoginMessageRead(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    ClientHandlerShared::runDbTask(
        this,
        [json]() -> QByteArray {
            // 1) 参数读取
            const QString account = json["account"].toString();
            const QJsonArray unreadMessagesArray = json["unreadMessages"].toArray();
            // 2) 参数校验
            if (account.isEmpty() || unreadMessagesArray.isEmpty()) return QByteArray();
            QList<QByteArray> messageIdBins;
            for (const QJsonValue &val : unreadMessagesArray) {
                const QString uuidStr = val.toString();
                if (uuidStr.isEmpty()) continue;
                const QUuid uuid = QUuid::fromString(uuidStr);
                if (uuid.isNull()) continue;
                messageIdBins.append(uuid.toRfc4122());
            }
            if (messageIdBins.isEmpty()) return QByteArray();
            // 3) DB 事务
            QSqlDatabase db = ConnectionPool::getInstance().getConnection();
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) return QByteArray();
            if (!qSqlStartTransaction(db)) return QByteArray();
            QStringList placeholders;
            placeholders.reserve(messageIdBins.size());
            for (int i = 0; i < messageIdBins.size(); ++i) {
                placeholders.append("?");
            }
            QSqlQuery qry(db);
            const QString sql = QString("UPDATE Messages SET status = ? WHERE receiver_id = ? AND message_id IN (%1)")
                                    .arg(placeholders.join(","));
            if (!qry.prepare(sql)) {
                qSqlRollback(db);
                return QByteArray();
            }
            qry.addBindValue("done");
            qry.addBindValue(account);
            for (const QByteArray &messageIdBin : messageIdBins) {
                qry.addBindValue(messageIdBin);
            }
            if (!qry.exec()) {
                qSqlRollback(db);
                return QByteArray();
            }
            if (!qSqlCommit(db)) {
                qSqlRollback(db);
                return QByteArray();
            }
            // 4) 返回执行标记
            return QByteArray("1");
        },
        [thisPtr](const QByteArray &) { (void)thisPtr; });
}

// 聊天 AI 分析：校验 session_token、单连接并发与按账号冷却；线程池内查库并调用 [AI] 配置的 HTTP 接口。
void ClientHandler::dealChatAiAnalyze(const QJsonObject &json)
{
    QPointer<ClientHandler> thisPtr(this);
    const QString requestId = json.value("request_id").toString();

    // 校验阶段直接回包（尚未设置 m_aiAnalyzeInFlight）。
    auto sendResult = [thisPtr](const QJsonObject &out) {
        if (thisPtr.isNull()) return;
        thisPtr->socketWrite(ClientHandlerShared::toJsonCompactBytes(out));
    };

    QJsonObject errShell;
    errShell["tag"] = QStringLiteral("chat_ai_analyze_result");
    errShell["request_id"] = requestId;
    errShell["ok"] = false;

    if (m_accountId == 0) {
        errShell["error"] = QStringLiteral("not_logged_in");
        sendResult(errShell);
        return;
    }

    quint64 accU64 = 0;
    if (!tryParseAccountId(json.value("account").toString(), accU64) || accU64 != m_accountId) {
        errShell["error"] = QStringLiteral("account_mismatch");
        sendResult(errShell);
        return;
    }

    const QString selfAccount = json.value("account").toString().trimmed();
    const QString peerAccount = json.value("peer").toString().trimmed();
    const int rangeValue = json.value("range_value").toInt();
    const QString unitRaw = json.value("range_unit").toString();
    const int rangeEndValue = json.value("range_end_value").toInt();
    const bool isCustomRange = unitRaw.trimmed().toLower().endsWith(QStringLiteral("_custom"));
    const QString unitNorm = normalizeAiRangeUnit(unitRaw);
    const QString rangeStartRaw = json.value(QStringLiteral("range_start_dt")).toString();
    const QString rangeEndRaw = json.value(QStringLiteral("range_end_dt")).toString();

    // 绝对时间点模式：由客户端传入起点/终点 datetime 字符串。
    auto parseAiAnalyzeDateTime = [](const QString &raw) -> QDateTime {
        const QString s = raw.trimmed();
        if (s.isEmpty())
            return {};
        // 支持：带毫秒 / 不带毫秒；以及两种分隔符。
        QDateTime dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        if (dt.isValid()) return dt;
        dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        if (dt.isValid()) return dt;
        dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
        if (dt.isValid()) return dt;
        dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        return dt;
    };

    const QDateTime absStartDt = parseAiAnalyzeDateTime(rangeStartRaw);
    const QDateTime absEndDt = parseAiAnalyzeDateTime(rangeEndRaw);
    const bool useAbsoluteRange = absStartDt.isValid() && absEndDt.isValid();
    QString userPrompt = json.value("prompt").toString();
    if (userPrompt.size() > 8000) {
        userPrompt = userPrompt.left(8000);
    }

    if (selfAccount.isEmpty() || peerAccount.isEmpty() || selfAccount == peerAccount) {
        errShell["error"] = QStringLiteral("invalid_peer");
        sendResult(errShell);
        return;
    }
    if (useAbsoluteRange) {
        if (absStartDt > absEndDt) {
            errShell["error"] = QStringLiteral("invalid_range");
            sendResult(errShell);
            return;
        }
    } else {
        if (rangeValue < 1 || rangeValue > 999 || unitNorm.isEmpty() || (isCustomRange && (rangeEndValue < 1 || rangeEndValue > 999))) {
            errShell["error"] = QStringLiteral("invalid_range");
            sendResult(errShell);
            return;
        }
        if (isCustomRange && rangeValue < rangeEndValue) {
            errShell["error"] = QStringLiteral("invalid_range");
            sendResult(errShell);
            return;
        }
    }

    const QString sessionTokClient = json.value(QStringLiteral("session_token")).toString();
    if (sessionTokClient.isEmpty() || sessionTokClient != m_sessionToken) {
        errShell["error"] = QStringLiteral("invalid_session");
        sendResult(errShell);
        return;
    }

    if (m_aiAnalyzeInFlight) {
        errShell["error"] = QStringLiteral("ai_busy");
        sendResult(errShell);
        return;
    }

    {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QMutexLocker locker(&g_aiAnalyzeRateMutex);
        qint64 &lastStart = g_aiAnalyzeAccountLastStartMs[selfAccount];
        if (lastStart > 0 && (nowMs - lastStart) < ServerConfigDefaults::aiAnalyzeCooldownMs()) {
            errShell["error"] = QStringLiteral("ai_rate_limited");
            sendResult(errShell);
            return;
        }
        lastStart = nowMs;
    }

    m_aiAnalyzeInFlight = true;

    ClientHandlerShared::dispatchBusinessTaskToThreadPool(
        this,
        [thisPtr, selfAccount, peerAccount, rangeValue, rangeEndValue, unitNorm, isCustomRange, userPrompt, requestId, useAbsoluteRange, absStartDt, absEndDt]() {
            QJsonObject out;
            out["tag"] = QStringLiteral("chat_ai_analyze_result");
            out["request_id"] = requestId;
            out["ok"] = false;

            const QDateTime nowDt = QDateTime::currentDateTime();
            const QDateTime startDt = useAbsoluteRange ? absStartDt : computeRangeStart(nowDt, rangeValue, unitNorm);
            const QDateTime endDt = useAbsoluteRange ? absEndDt : (isCustomRange ? computeRangeStart(nowDt, rangeEndValue, unitNorm) : nowDt);
            if (!startDt.isValid() || !endDt.isValid() || startDt > endDt) {
                out["error"] = QStringLiteral("invalid_range");
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }
            const QString tsStart = startDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
            const QString tsEnd = endDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));

            QString apiUrlStr, apiKey, model;
            int timeoutMs = 0, maxMsg = 0, maxChars = 0;
            {
                apiUrlStr = ServerConfigDefaults::aiApiUrl();
                apiKey = ServerConfigDefaults::aiApiKey();
                model = ServerConfigDefaults::aiModel();
                timeoutMs = ServerConfigDefaults::aiTimeoutMs();
                maxMsg = ServerConfigDefaults::aiMaxMessages();
                maxChars = ServerConfigDefaults::aiMaxTranscriptChars();
            }

            QSqlDatabase db = ConnectionPool::getInstance().getConnection(qMax(5000, timeoutMs / 2));
            DbConnectionGuard guard(db);
            if (!db.isValid() || !db.isOpen()) {
                out["error"] = QStringLiteral("db_unavailable");
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }

            QSqlQuery checkQry(db);
            checkQry.prepare(
                "SELECT COUNT(*) FROM Friends WHERE (user_id = :u1 AND friend_id = :f1) OR (user_id = :u2 AND friend_id = :f2)");
            checkQry.bindValue(QStringLiteral(":u1"), selfAccount);
            checkQry.bindValue(QStringLiteral(":f1"), peerAccount);
            checkQry.bindValue(QStringLiteral(":u2"), peerAccount);
            checkQry.bindValue(QStringLiteral(":f2"), selfAccount);
            if (!checkQry.exec() || !checkQry.next() || checkQry.value(0).toInt() == 0) {
                out["error"] = QStringLiteral("not_friend");
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }

            QSqlQuery qry(db);
            qry.setForwardOnly(true);
            const QString sql =
                QStringLiteral(
                    "SELECT sender_id, receiver_id, content, message_type, filename, timestamp FROM Messages "
                    "WHERE ((sender_id = :a AND receiver_id = :b) OR (sender_id = :b2 AND receiver_id = :a2)) "
                    "AND timestamp >= :t0 AND timestamp <= :t1 ORDER BY timestamp ASC LIMIT %1")
                    .arg(maxMsg);
            qry.prepare(sql);
            qry.bindValue(QStringLiteral(":a"), selfAccount);
            qry.bindValue(QStringLiteral(":b"), peerAccount);
            qry.bindValue(QStringLiteral(":b2"), peerAccount);
            qry.bindValue(QStringLiteral(":a2"), selfAccount);
            qry.bindValue(QStringLiteral(":t0"), tsStart);
            qry.bindValue(QStringLiteral(":t1"), tsEnd);
            if (!qry.exec()) {
                out["error"] = QStringLiteral("query_failed");
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }

            QString transcript;
            int rows = 0;
            while (qry.next()) {
                const QString line = formatChatLineForAi(
                    selfAccount,
                    qry.value(QStringLiteral("sender_id")).toString(),
                    qry.value(QStringLiteral("message_type")).toString(),
                    qry.value(QStringLiteral("content")).toString(),
                    qry.value(QStringLiteral("filename")).toString(),
                    qry.value(QStringLiteral("timestamp")).toString());
                transcript.append(line);
                ++rows;
            }

            if (transcript.isEmpty()) {
                out["error"] = QStringLiteral("no_messages_in_range");
                out["message_count"] = 0;
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }

            if (transcript.size() > maxChars) {
                const int cut = transcript.size() - maxChars;
                transcript = QStringLiteral("【记录过长，已省略开头约 %1 字】\n").arg(cut) + transcript.right(maxChars);
            }

            const QString systemPrompt =
                QStringLiteral("你是聊天助理。根据用户给出的指令与聊天记录，用简体中文输出结论；若信息不足请说明。不要编造对话中不存在的事实。");
            const QString userBlock =
                userPrompt + QStringLiteral("\n\n--- 会话双方账号：") + selfAccount + QStringLiteral(" 与 ")
                + peerAccount + QStringLiteral(" ---\n时间范围：自 ") + tsStart + QStringLiteral(" 至 ") + tsEnd
                + QStringLiteral("\n共 ") + QString::number(rows) + QStringLiteral(" 条消息。\n\n--- 聊天记录 ---\n")
                + transcript;

            const QUrl apiUrl(apiUrlStr);
            QString llmErr;
            const QString answer = callOpenAiCompatibleChat(apiUrl, apiKey, model, systemPrompt, userBlock,
                                                              timeoutMs, &llmErr);
            if (answer.isEmpty()) {
                out["error"] = llmErr.isEmpty() ? QStringLiteral("llm_failed") : llmErr;
                out["message_count"] = rows;
                ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                    if (thisPtr.isNull()) return;
                    thisPtr->postAiAnalyzeResult(out);
                });
                return;
            }

            out["ok"] = true;
            out["message_count"] = rows;
            out["result"] = answer;
            ClientHandlerShared::runOnObjectThread(thisPtr.data(), [thisPtr, out]() {
                if (thisPtr.isNull()) return;
                thisPtr->postAiAnalyzeResult(out);
            });
        });
}

// ===== 转发 =====
// 转发好友申请
void ClientHandler::forwordAddFriendRequest(const QJsonObject &json)
{
    const QString account = json["account"].toString();
    QSqlDatabase db = ConnectionPool::getInstance().getConnection();
    DbConnectionGuard guard(db);
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "forwordAddFriendRequest: db connection invalid";
        return;
    }
    QJsonObject qjsonObj;
    qjsonObj["tag"] = "newaddrequest";
    QSqlQuery qry(db);
    qry.setForwardOnly(true);
    qry.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
    qry.bindValue(":account", account);
    if (qry.exec() && qry.next()) {
        qjsonObj["account_number"] = qry.value("account_number").toString();
        qjsonObj["nickname"] = qry.value("nickname").toString();
        qjsonObj["gender"] = qry.value("gender").toString();
        qjsonObj["signature"] = qry.value("signature").toString();
        QString avatorUrl = qry.value("avator").toString();
        qjsonObj["avator"] = avatorUrl;
        qjsonObj["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(m_savePath, avatorUrl);
    }
    sendJsonToSocketQueued(qjsonObj);
}

// 转发申请通过
void ClientHandler::forwordRequestPass(const QJsonObject &json)
{
    const QString account = json["account"].toString();
    QSqlDatabase db = ConnectionPool::getInstance().getConnection();
    DbConnectionGuard guard(db);
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "forwordRequestPass: db connection invalid";
        return;
    }
    QJsonObject qjsonObj;
    qjsonObj["tag"] = "addrequestpass";
    QSqlQuery qry(db);
    qry.setForwardOnly(true);
    qry.prepare("SELECT account_number, nickname, gender, signature, avator FROM Users WHERE account_number = :account");
    qry.bindValue(":account", account);
    if (qry.exec() && qry.next()) {
        qjsonObj["account_number"] = qry.value("account_number").toString();
        qjsonObj["nickname"] = qry.value("nickname").toString();
        qjsonObj["gender"] = qry.value("gender").toString();
        qjsonObj["signature"] = qry.value("signature").toString();
        QString avatorUrl = qry.value("avator").toString();
        qjsonObj["avator"] = avatorUrl;
        qjsonObj["avatorbase64"] = ClientHandlerShared::getAvaFromUrlStatic(m_savePath, avatorUrl);
    }
    sendJsonToSocketQueued(qjsonObj);
}

// 转发被删除通知。
void ClientHandler::forwordYouAreDeleted(const QJsonObject &json)
{
    sendJsonToSocketQueued(json);
}

// 转发挤下线通知。
void ClientHandler::forwordKickedOffline(const QJsonObject &json)
{
    sendJsonToSocketQueued(json);
}

// 转发聊天消息。
void ClientHandler::forwordMessages(const QJsonObject &json)
{
    sendJsonToSocketQueued(json);
}

// 转发资料更新
void ClientHandler::forwordChangeInfor(const QJsonObject &json)
{
    sendJsonToSocketQueued(json);
}