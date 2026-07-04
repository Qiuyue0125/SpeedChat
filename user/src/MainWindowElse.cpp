/**
 * @file MainWindowElse.cpp
 * 主窗口辅助：委托绘制、好友右键菜单与头像/昵称在消息行中的行为。
 */
#include "MainWindowElse.h"
#include "FriendMessage.h"
#include "AvatarManager.h"
#include "AccountMessageManager.h"
#include <QDate>
#include <QDateTime>
#include <QFontMetrics>

// 聊天/会话列表时间戳：ISO、毫秒、epoch、多种分隔符（与 processTimestamp 同源）
QDateTime parseFlexibleChatTimestamp(const QString& rawTimestamp)
{
    if (rawTimestamp.trimmed().isEmpty() || rawTimestamp == QLatin1String("notime")) {
        return {};
    }

    QString processedTime = rawTimestamp.trimmed();
    QDateTime dt;

    bool isNumber = false;
    const qlonglong timestampNum = processedTime.toLongLong(&isNumber);
    if (isNumber) {
        if (processedTime.length() == 10) {
            dt = QDateTime::fromSecsSinceEpoch(timestampNum);
        } else if (processedTime.length() == 13) {
            dt = QDateTime::fromMSecsSinceEpoch(timestampNum);
        } else {
            return {};
        }
        return dt;
    }

    processedTime.replace('T', ' ');
    processedTime.remove(QRegularExpression(R"(\+(\d{2}):(\d{2})|Z)"));

    const int dotIndex = processedTime.indexOf('.');
    if (dotIndex > 0) {
        processedTime = processedTime.left(dotIndex);
    }

    const QStringList timeFormats = {
        QStringLiteral("yyyy-MM-dd hh:mm:ss"),
        QStringLiteral("yyyy-MM-dd hh:mm"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("yyyy/MM/dd hh:mm:ss"),
        QStringLiteral("yyyy/MM/dd hh:mm"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
    };
    for (const QString& fmt : timeFormats) {
        dt = QDateTime::fromString(processedTime.trimmed(), fmt);
        if (dt.isValid()) {
            return dt;
        }
    }
    return {};
}

QString chineseDayPeriodForHour(int hour)
{
    if (hour >= 0 && hour < 6)
        return QStringLiteral("凌晨");
    if (hour < 12)
        return QStringLiteral("上午");
    if (hour < 18)
        return QStringLiteral("下午");
    return QStringLiteral("晚上");
}

QString formatChineseChatDateOnly(const QDate &date)
{
    const int yNow = QDate::currentDate().year();
    if (date.year() == yNow)
        return QStringLiteral("%1月%2日").arg(date.month()).arg(date.day());
    return QStringLiteral("%1年%2月%3日").arg(date.year()).arg(date.month()).arg(date.day());
}

// 菜单事件
void TalkList::contextMenuEvent(QContextMenuEvent *event)
{
    // 获取索引
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        setCurrentIndex(index);
    }

    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->setStyleSheet(
        "QMenu {"
        "   background-color: rgb(240, 240, 240);"
        "   border-radius: 3px;"
        "   border: 0.5px solid rgb(0, 0, 0);"
        "}"
        "QMenu::item {"
        "   padding: 10px;"
        "   text-align: left;"
        "}"
        "QMenu::item:selected {"
        "   background-color: rgb(200, 200, 200);"
        "}"
        "QMenu::item:pressed {"
        "   background-color: rgb(180, 180, 180);"
        "}");

    // 添加菜单项
    QAction *actionAi = menu->addAction(QStringLiteral("AI分析"));
    QAction *action1 = menu->addAction("发送消息");
    QAction *action2 = menu->addAction("查看资料");
    QAction *action3 = menu->addAction("删除好友");
    QAction *action4 = menu->addAction("移除消息列表");
    QAction *action5 = menu->addAction("清空聊天记录");

    connect(action1, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "sendmessage";
        emit choiceDone(json);
    });

    connect(action2, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "message";
        emit choiceDone(json);
    });

    connect(action3, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "deletefriend";
        emit choiceDone(json);
    });

    connect(action4, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "deletetalk";
        emit choiceDone(json);
    });

    connect(action5, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "clearmessage";
        emit choiceDone(json);
    });

    connect(actionAi, &QAction::triggered, this, [this]() {
        QJsonObject json;
        json["tag"] = "aianalyze";
        emit choiceDone(json);
    });

    menu->popup(event->globalPos());
}

// 鼠标按下
void FriendList::mousePressEvent(QMouseEvent *event)
{
    QListView::mousePressEvent(event);
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
            setCurrentIndex(index);
            clicked(index);
        }
    }
}

// 绘制列表项
void FriendDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!index.isValid()) return;
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    QString name;
    const int row = index.row();
    QColor backgroundColor = option.palette.window().color();

    if (row == 1 && index.data(Qt::UserRole + 20).toBool()) {
        backgroundColor = QColor(255, 105, 180);
    } else if (option.state & QStyle::State_Selected) {
        backgroundColor = QColor(255, 220, 240);
    } else if ((option.state & QStyle::State_MouseOver) && row != 0 && row != 2) {
        backgroundColor = QColor(240, 240, 240);
    }

    painter->setBrush(backgroundColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(option.rect, 10, 10);

    if(row == 0){
        name = "新的好友";
    }
    else if (row == 2) {
        name = "好友列表";
    }
    else {
        QPixmap pixmap;
        if (row == 1) {
            pixmap = QPixmap(":/pictures/addfriends.jpg").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pixmap = AvatarManager::getRoundedPixmap(pixmap, 5);
            name = "好友申请";
        } else {
            pixmap = AvatarManager::getInstance()->loadAvator(
                index.data(Qt::UserRole + 1).toString(),
                QSize(40, 40), 5
                );
            name = AccountMessageManager::getInstance()->getInfo(index.data(Qt::UserRole + 1).toString()).name;
        }

        const int iconHeight = 40;
        const int y = option.rect.y() + (option.rect.height() - iconHeight) / 2;
        painter->drawPixmap(QRect(option.rect.x() + 5, y, iconHeight, iconHeight), pixmap);
    }

    QColor textColor = option.palette.text().color();
    QFont font = painter->font();
    int adjustLeft = 50;

    if (row == 0 || row == 2) {
        textColor = QColor(60, 60, 60);
        font.setPointSize(10);
        font.setBold(true);
        adjustLeft = 5;
    }

    painter->setFont(font);
    painter->setPen(textColor);
    painter->drawText(
        option.rect.adjusted(adjustLeft, 0, 0, 0),
        Qt::AlignVCenter | Qt::TextSingleLine,
        name
        );

    painter->restore();
}

// 计算尺寸
QSize FriendDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    int row = index.row();
    if(row == 2 || row == 0) {
        return QSize(170, 20);
    }
    else
        return QSize(170, 56);
}

// 绘制列表项
void TalkDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!index.isValid()) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    const QString talkAccount = index.data(Qt::UserRole + 1).toString();
    AccountInfo& finfo = AccountMessageManager::getInstance()->getInfo(talkAccount);
    QString name = finfo.name;
    if (name.isEmpty())
        name = finfo.account;
    if (name.isEmpty())
        name = talkAccount;
    QColor backgroundColor;
    if (option.state & QStyle::State_Selected) {
        backgroundColor = QColor(255, 220, 240);
    } else if (option.state & QStyle::State_MouseOver) {
        backgroundColor = QColor(240, 240, 240);
    } else {
        backgroundColor = option.palette.window().color();
    }
    painter->setBrush(backgroundColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(option.rect, 10, 10);
    QPixmap pixmap = AvatarManager::getInstance()->loadAvator(talkAccount, QSize(40,40), 5);
    int y = option.rect.y() + (option.rect.height() - 40) / 2;
    QRect iconRect(option.rect.x() + 5, y, 40, 40);
    painter->drawPixmap(iconRect, pixmap);

    const int textX = 49;
    const int textY = option.rect.y() + 10;
    const int gap = 4;

    QString dataInfo = index.data(Qt::UserRole + 6).toString();
    dataInfo = displayTimeComparison(dataInfo);
    const QRect dateRect(option.rect.x() + option.rect.width() - 106, textY - 5, 98, option.rect.height() / 2);

    QFont msgFont(QStringLiteral("Microsoft YaHei UI Light"), 9);
    msgFont.setBold(true);

    // 昵称：字号固定，始终完整绘制（不省略号）；与日期冲突时只缩小日期字号。
    QFont nameFont(QStringLiteral("Microsoft YaHei UI Light"), 9);
    nameFont.setWeight(QFont::Normal);
    const QFontMetrics nameFm(nameFont);
    const int nameWpx = nameFm.horizontalAdvance(name);

    QFont dateFont = msgFont;
    int datePt = qMax(5, msgFont.pointSize() - 2);
    const bool shortDate = (dataInfo.size() < 8);

    // 时间戳两行显示规则：
    // - 当格式化结果形如“昨天上午 09:30 / 晚上 22:10”等且前缀较长时，拆成两行：
    //   第一行：日期或「时段 + 昨天」前缀；第二行：hh:mm
    // - “下午 15:20”等较短情况保持单行，避免高度变化。
    QString dateLine1 = dataInfo;
    QString dateLine2;
    const int lastSpace = dataInfo.lastIndexOf(QLatin1Char(' '));
    const bool useTwoLines = (lastSpace > 0 && lastSpace < dataInfo.size() - 1
                               && dataInfo.left(lastSpace).size() > 2);
    if (useTwoLines) {
        dateLine1 = dataInfo.left(lastSpace);
        dateLine2 = dataInfo.mid(lastSpace + 1);
    }
    const QString dateTextForWidth = useTwoLines ? dateLine1 : dataInfo;

    while (true) {
        dateFont.setPointSize(datePt);
        const int dw = QFontMetrics(dateFont).horizontalAdvance(dateTextForWidth);
        const int dateTextLeft = dateRect.x() + dateRect.width() - dw;
        const int nameAvail = dateTextLeft - textX - gap;
        if (nameWpx <= nameAvail || datePt <= 5) {
            break;
        }
        datePt -= 1;
    }
    dateFont.setPointSize(qMax(5, datePt));

    // 先画日期再画昵称：日期字号已尽量缩小；若仍略重叠，昵称后画保证全称可读。
    painter->setFont(dateFont);
    painter->setPen(QColor(100, 100, 100));
    if (useTwoLines) {
        // 上半行显示日期或时段前缀，下半行显示“hh:mm”
        const int halfH = dateRect.height() / 2;
        const QRect upper(dateRect.x(), dateRect.y(), dateRect.width(), halfH);
        const QRect lower(dateRect.x(), dateRect.y() + halfH, dateRect.width(), dateRect.height() - halfH);
        painter->drawText(upper, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, dateLine1);
        painter->drawText(lower, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, dateLine2);
    } else {
        painter->drawText(dateRect, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, dataInfo);
    }

    painter->setFont(nameFont);
    painter->setPen(QColor(Qt::black));
    painter->drawText(textX, textY + nameFm.ascent(), name);

    const QString messageInfoRaw = index.data(Qt::UserRole + 5).toString();
    painter->setFont(msgFont);
    painter->setPen(QColor(130, 130, 130));
    const int msgLineWidth = qMax(0, option.rect.width() - textX - 50);
    // 当时间戳启用两行时，日期下半行可能与消息区域发生纵向重叠；
    // 将消息起点下移到下半区边界，避免覆盖时间戳绘制内容。
    const int bottomHalfY = option.rect.y() + option.rect.height() / 2;
    const int msgLineY = useTwoLines ? qMax(textY + nameFm.height() + 2, bottomHalfY)
                                      : textY + nameFm.height() + 2;
    const QRect msgRect(textX, msgLineY, msgLineWidth, option.rect.height() / 2);
    const QString messageInfo = QFontMetrics(msgFont).elidedText(
        messageInfoRaw, Qt::ElideRight, msgLineWidth);
    painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, messageInfo);

    QFont tailFont = dateFont;
    if (shortDate) {
        tailFont.setPointSize(qMax(5, tailFont.pointSize() - 3));
    }
    QString newMessage = index.data(Qt::UserRole + 10).toString();
    QColor bgColor(255, 100, 100);
    int cornerRadius = 10;
    int rectWidth = 16;
    int rectHeight = 16;
    int num = newMessage.toInt();
    if (num == 0){
        painter->restore();
        return;
    } else if(num > 99){
        newMessage = "99+";
        rectWidth = 24;
    }
    QRect roundedRect(option.rect.x() + 2, option.rect.y() + 2, rectWidth, rectHeight);
    painter->setBrush(bgColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(roundedRect, cornerRadius, cornerRadius);
    painter->setPen(Qt::white);
    tailFont.setBold(true);
    tailFont.setPointSize(qMax(6, tailFont.pointSize() + 1));
    painter->setFont(tailFont);
    painter->drawText(roundedRect, Qt::AlignCenter, newMessage);
    painter->restore();
}

// 计算尺寸
QSize TalkDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return QSize(188, 56);
}

// 格式化时间
QString TalkDelegate::displayTimeComparison(const QString &timestampStr) const
{
    const QDateTime timestamp = parseFlexibleChatTimestamp(timestampStr);
    if (!timestamp.isValid()) {
        return QString();
    }
    QDateTime current = QDateTime::currentDateTime();
    QDate dateToday = current.date();
    QDate dateTimestamp = timestamp.date();
    QString result;
    if (dateToday == dateTimestamp) {
        QString timePart = timestamp.time().toString("hh:mm");
        QString period = chineseDayPeriodForHour(timestamp.time().hour());
        result = QString("%1 %2").arg(period).arg(timePart);
    }
    else if (dateToday == dateTimestamp.addDays(1)) {
        QString timePart = timestamp.time().toString("hh:mm");
        QString period = chineseDayPeriodForHour(timestamp.time().hour());
        result = QString("昨天%1 %2").arg(period).arg(timePart);
    }
    else {
        // 早于昨天：列表只显示到日期（无时段、无时刻）
        result = formatChineseChatDateOnly(dateTimestamp);
    }
    return result;
}

// 判断是否显示（好友列表过滤）
bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (sourceRow < 3) {
        return true;
    }
    const QString filter = filterRegularExpression().pattern();
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    QString account = index.data(Qt::UserRole + 1).toString();
    QString nickname = AccountMessageManager::getInstance()->getInfo(account).name;

    bool accepts = nickname.contains(filter, Qt::CaseInsensitive) ||
                   account.contains(filter, Qt::CaseInsensitive);
    return accepts;
}

// 判断是否显示（聊天列表过滤）
bool TalkFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QString filter = filterRegularExpression().pattern();
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    QString account = index.data(Qt::UserRole + 1).toString().trimmed();
    if (account.isEmpty())
        return false;
    QString nickname = AccountMessageManager::getInstance()->getInfo(account).name;

    bool accepts = nickname.contains(filter, Qt::CaseInsensitive) ||
                   account.contains(filter, Qt::CaseInsensitive);
    return accepts;
}

// 获取焦点
void LineSerach::focusInEvent(QFocusEvent *event)
{
    setStyleSheet("font: 10pt 'Microsoft YaHei UI'; "
                  "border-radius: 3px; "
                  "background:white;"
                  "color: black;"
                  "padding: 5px;");
    if(text() == "搜索"){
        setText("");
    }
    QLineEdit::focusInEvent(event);
}

// 失去焦点
void LineSerach::focusOutEvent(QFocusEvent *event)
{
    setStyleSheet("font: 10pt 'Microsoft YaHei UI'; "
                  "border-radius: 3px; "
                  "background:rgb(245, 245, 245);"
                  "color: grey;"
                  "padding: 5px;");
    if (text().isEmpty()) {
        setText("搜索");
    }
    QLineEdit::focusOutEvent(event);
}

// 鼠标按下
void LabelAva::mousePressEvent(QMouseEvent *event)
{
    emit changeInfo();
    QLabel::mousePressEvent(event);
}

// 鼠标按下
void LabelFriendAva::mousePressEvent(QMouseEvent *event)
{
    QLabel::mousePressEvent(event);
    if(!m_flag) return;
    QString account = property("account").toString();
    FriendMessage *dialog = new FriendMessage(account,this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);  //关闭时自动删除
    dialog->show();
}

// 鼠标按下
void LabelFriendAvaInMessage::mousePressEvent(QMouseEvent *event)
{
    emit showMessage();
    QLabel::mousePressEvent(event);
}

// 设置背景
void CustomTextEdit::setBackground(const QPixmap& pix)
{
    m_background = pix;
}

// 恢复背景
void CustomTextEdit::setDefaultBack()
{
    m_background = QPixmap(m_defaultBackground);
}

// 设置默认背景路径
void CustomTextEdit::setDefaultBackgroundPath(const QString &path)
{
    m_defaultBackground = path.isEmpty() ? QString(":/pictures/tafei.png") : path;
    // 刷新时恢复到默认背景
    setDefaultBack();
    viewport()->update();
}

// 绘制背景
void CustomTextEdit::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);//反锯齿

    if (!m_background.isNull()) {
        qreal scaleX = qreal(viewport()->width()) / m_background.width();
        qreal scaleY = qreal(viewport()->height()) / m_background.height();
        qreal scale = qMax(scaleX, scaleY);

        int scaledWidth = m_background.width() * scale;
        int scaledHeight = m_background.height() * scale;

        int x = (viewport()->width() - scaledWidth) / 2;
        int y = (viewport()->height() - scaledHeight) / 2;

        painter.drawPixmap(x, y, scaledWidth, scaledHeight, m_background);
    }

    //创建一个蒙版，设置透明度
    painter.setPen(Qt::transparent);
    painter.setBrush(QColor(255, 255, 255, 150));
    painter.drawRect(0, 0, viewport()->width(), viewport()->height());
    QTextEdit::paintEvent(event);
}

// 设置背景
void TalkStacked::setBackground(const QPixmap &pix)
{
    m_background = pix;
    update();
}

// 恢复默认背景
void TalkStacked::setDefaultBack()
{
    QPixmap pix(m_defaultBackground.isEmpty() ? QString(":/pictures/tafei.png") : m_defaultBackground);
    if (!pix.isNull()) {
        m_background = pix;
        update();
    }
}

// 设置默认背景路径
void TalkStacked::setDefaultBackgroundPath(const QString &path)
{
    m_defaultBackground = path.isEmpty() ? QString(":/pictures/tafei.png") : path;
}

// 绘制背景
void TalkStacked::paintEvent(QPaintEvent *event)
{
    if (m_background.isNull()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    //圆角参数
    const int borderRadius = 10;
    const int borderWidth = 4;
    const QColor borderColor = QColor(255, 153, 179, 255);
    painter.save();

    //裁剪区域仅作用于背景图和蒙版，绘制后立即恢复状态
    QPainterPath clipPath;
    clipPath.addRoundedRect(this->rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth),
                            borderRadius - 1, borderRadius - 1);
    painter.setClipPath(clipPath);

    //绘制背景图
    QSize targetSize = this->rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth).size();
    QSize imgSize = m_background.size();
    const qreal scale = qMax((qreal)targetSize.width()/imgSize.width(), (qreal)targetSize.height()/imgSize.height());
    QPixmap scaledBackground = m_background.scaled(imgSize * scale, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int xOffset = (scaledBackground.width() - targetSize.width()) / 2;
    const int yOffset = (scaledBackground.height() - targetSize.height()) / 2;

    painter.drawPixmap(this->rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth),
                       scaledBackground,
                       QRect(xOffset, yOffset, targetSize.width(), targetSize.height()));

    //在背景图上添加半透明白色蒙版
    painter.setPen(Qt::transparent);
    painter.setBrush(QColor(255, 255, 255, 100));
    painter.drawRect(this->rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth));

    painter.restore();

    //绘制圆角边框（用 fillPath + 布尔运算，外沿半径 == borderRadius，与 CSS border-radius 语义一致）
    painter.save();
    QPainterPath outerPath;
    outerPath.addRoundedRect(this->rect(), borderRadius, borderRadius);
    QPainterPath innerPath;
    innerPath.addRoundedRect(this->rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth),
                              borderRadius - borderWidth, borderRadius - borderWidth);
    QPainterPath borderPath = outerPath.subtracted(innerPath);
    painter.fillPath(borderPath, borderColor);
    painter.restore();
    QStackedWidget::paintEvent(event);
}

// 图片消息控件构造函数
ImageLabel::ImageLabel(const QString &pixmapPath, QWidget *parent)
    : QLabel(parent), m_savePath(pixmapPath) {
    setCursor(Qt::PointingHandCursor);
}

// 鼠标按下
void ImageLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        QDesktopServices::openUrl(QUrl::fromLocalFile(m_savePath));
    }
    QLabel::mousePressEvent(event);
}

// 菜单事件
void ImageLabel::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_savePath.isEmpty() || !QFile::exists(m_savePath)) {
        QLabel::contextMenuEvent(event);
        return;
    }

    QMenu *contextMenu = new QMenu(this);
    contextMenu->setAttribute(Qt::WA_DeleteOnClose);

    contextMenu->setStyleSheet(
        "QMenu {"
        "   background-color: rgb(240, 240, 240);"
        "   border-radius: 3px;"
        "   border: 0.5px solid rgb(0, 0, 0);"
        "}"
        "QMenu::item {"
        "   padding: 10px;"
        "   text-align: left;"
        "   color: rgb(0, 0, 0);"
        "}"
        "QMenu::item:selected {"
        "   background-color: rgb(200, 200, 200);"
        "   color: rgb(0, 0, 0);"
        "}"
        "QMenu::item:pressed {"
        "   background-color: rgb(180, 180, 180);"
        "   color: rgb(0, 0, 0);"
        "}"
        );

    QAction *saveAction = contextMenu->addAction("另存为...");

    connect(saveAction, &QAction::triggered, this, [this]() {
        QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                        "另存为",
                                                        QFileInfo(m_savePath).fileName(),
                                                        "Images (*.png *.jpg *.jpeg *.bmp)");
        if (fileName.isEmpty()) {
            return;
        }

        QThread* saveThread = new QThread(this);
        QObject* worker = new QObject;
        worker->moveToThread(saveThread);

        QString srcPath = m_savePath;

        connect(saveThread, &QThread::started, worker, [=]() {
            QImage image(srcPath);
            if (image.isNull()) {
                qWarning() << "[ImageLabel] 异步保存：加载原图片失败" << srcPath;
                saveThread->quit();
                return;
            }

            bool saveOk = image.save(fileName);
            if (!saveOk) {
                qWarning() << "[ImageLabel] 异步保存：图片保存失败" << fileName;
            }

            saveThread->quit();
        });

        connect(saveThread, &QThread::finished, worker, &QObject::deleteLater);
        connect(saveThread, &QThread::finished, saveThread, &QThread::deleteLater);

        saveThread->start();
    });

    contextMenu->popup(event->globalPos());
    QLabel::contextMenuEvent(event);
}

// 语音消息控件构造函数
AudioLabel::AudioLabel(QString time, const QString &audioPath, QWidget *parent)
    : QLabel(parent), m_audioPath(audioPath) {
    this->setText("语音:" + time);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
}

// 鼠标按下
void AudioLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit startAudio(m_audioPath);
    }
    QLabel::mousePressEvent(event);
}

// 键盘按下
void EnterTextEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        //如果同时按下了Ctrl，则只接受回车
        if (event->modifiers() & Qt::ControlModifier) {
            this->insertPlainText("\n");
            return;
        } else {
            emit enterKey();//只有回车则发送消息
            return;
        }
    }
    //调用基类的处理函数
    QTextEdit::keyPressEvent(event);
}
