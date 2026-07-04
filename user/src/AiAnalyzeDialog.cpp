/**
 * @file AiAnalyzeDialog.cpp
 * 聊天 AI 分析范围与参数选择对话框。
 */
#include "AiAnalyzeDialog.h"
#include "ChatScrollStyle.h"
#include "ui_AiAnalyzeDialog.h"
#include "CloseButtonUtils.h"

#include <QIcon>
#include <QIntValidator>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QScrollBar>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QCursor>
#include <QCalendarWidget>
#include <QFont>
#include <QToolButton>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>

const QString AiAnalyzeDialog::BTN_ENABLE_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(5, 186, 251);
        color: white;
        border-radius: 10px;
        border: 0.5px solid rgb(220, 220, 220);
    }
    QPushButton:hover {
        background-color: rgba(5, 186, 251, 0.7);
        color: white;
        border-radius: 10px;
    }
    QPushButton:pressed {
        background-color: rgba(0, 123, 255, 0.8);
        color: rgba(255, 255, 255, 0.9);
        border-radius: 10px;
    }
)";

namespace {
// QDateTimeEdit（输入框区域）样式：统一边框、圆角、字体与背景。
QString dtEditStyleSheet()
{
    return QStringLiteral(R"(
QDateTimeEdit {
    font: 12pt 'Microsoft YaHei UI';
    border: 1px solid rgba(0, 0, 0, 0.1);
    border-radius: 8px;
    padding: 5px;
    color: black;
    background-color: rgba(255, 255, 255, 0.98);
    outline: none;
}
QDateTimeEdit:hover {
    border: 1px solid rgba(0, 0, 0, 0.1);
    outline: none;
}
QDateTimeEdit:focus {
    border: 1px solid rgba(0, 0, 0, 0.1);
    outline: none;
}
QDateTimeEdit:pressed {
    border: 1px solid rgba(0, 0, 0, 0.1);
    outline: none;
}
QDateTimeEdit::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 30px;
    border-left: 1px solid rgba(0, 0, 0, 0.08);
    background-color: rgba(255, 255, 255, 0.98);
    border-top-right-radius: 8px;
    border-bottom-right-radius: 8px;
}
QDateTimeEdit::down-arrow {
    width: 0px;
    height: 0px;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 0px solid transparent;
    border-bottom: 6px solid rgba(0, 0, 0, 0.6);
    margin-right: 10px;
}
QDateTimeEdit::up-arrow {
    width: 0px;
    height: 0px;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-bottom: 0px solid transparent;
    border-top: 6px solid rgba(0, 0, 0, 0.6);
    margin-right: 10px;
}
)");
}

// QCalendarWidget（日历弹窗）样式：白底深灰文字、选中蓝色。
QString calendarStyleSheet()
{
    // 给弹出日历本体直接 setStyleSheet，避免被上层（父控件）QSS 影响。
    return QStringLiteral(R"(
QCalendarWidget {
    background-color: white;
    alternate-background-color: white;
    border: 1px solid rgba(0, 0, 0, 0.1);
    border-radius: 0px;
    color: #4a4a4a;
    font: 12pt 'Microsoft YaHei UI';
    padding: 0px;
}
QCalendarWidget QWidget {
    background-color: white;
    color: #4a4a4a;
    border: none;
    border-radius: 0px;
}
QCalendarWidget QHeaderView {
    background-color: white;
    border: none;
}
QCalendarWidget QLabel {
    background-color: white;
    color: #4a4a4a;
    border: none;
}
QCalendarWidget QToolButton {
    background-color: transparent;
    border: none;
    color: #4a4a4a;
}
QCalendarWidget QComboBox {
    background-color: white;
    border: none;
    color: #4a4a4a;
}
QCalendarWidget QComboBox::drop-down {
    border: none;
    background: transparent;
    width: 0px;
    min-width: 0px;
    max-width: 0px;
    padding: 0px;
    height: 0px;
}
QCalendarWidget QSpinBox {
    background-color: white;
    border: none;
    color: #4a4a4a;
}
QCalendarWidget QSpinBox::up-button,
QCalendarWidget QSpinBox::down-button {
    width: 16px;
    border: none;
    background: transparent;
}
QCalendarWidget QAbstractSpinBox::up-arrow,
QCalendarWidget QAbstractSpinBox::down-arrow {
    width: 0px;
    height: 0px;
    border: none;
}

QCalendarWidget QAbstractSpinBox::down-arrow {
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid rgba(0, 0, 0, 0.6);
}

QCalendarWidget QAbstractSpinBox::up-arrow {
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid rgba(0, 0, 0, 0.6);
}
QCalendarWidget QAbstractItemView {
    background-color: white;
    border: none;
    color: #4a4a4a;
    gridline-color: rgba(0, 0, 0, 0.06);
    font: 12pt 'Microsoft YaHei UI';
}
QCalendarWidget QAbstractItemView::item {
    background-color: white;
    border: none;
    color: #4a4a4a;
    min-height: 30px;
    border-radius: 0px;
}
QCalendarWidget QAbstractItemView::item:selected {
    background-color: rgb(5, 186, 251);
    color: white;
    border-radius: 0px;
}
QCalendarWidget QAbstractItemView::item:hover {
    background-color: rgba(5, 186, 251, 0.12);
    color: #4a4a4a;
    border-radius: 0px;
}
)");
}

// 初始化并统一 QCalendarWidget 外观
void setupCalendarWidget(QCalendarWidget *cal)
{
    if (!cal) return;

    cal->setNavigationBarVisible(true); // 保留年月文字
    cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader); // 去掉最左侧序号
    cal->setStyleSheet(calendarStyleSheet());
    cal->setMinimumSize(QSize(450, 380));

    for (QToolButton *btn : cal->findChildren<QToolButton *>()) {
        if (!btn) continue;
        const QSize s = btn->sizeHint();
        const bool hasNoText = btn->text().trimmed().isEmpty();
        const bool isSmallEnough = (s.width() <= 40 && s.height() <= 40) || (btn->width() <= 40 && btn->height() <= 40);
        if (hasNoText && isSmallEnough) btn->setVisible(false);
    }

    // 年份/月份区域内部包含 QAbstractSpinBox（年份）。
    QSpinBox *yearSpin = cal->findChild<QSpinBox *>();
    for (QAbstractSpinBox *sb : cal->findChildren<QAbstractSpinBox *>()) {
        sb->setButtonSymbols((yearSpin && sb == yearSpin) ? QAbstractSpinBox::UpDownArrows : QAbstractSpinBox::NoButtons);
        sb->setFixedWidth(120);
    }
    if (yearSpin) {
        yearSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    }

    // 增大年月下拉区域宽度，避免点击后文字不全
    for (QComboBox *cb : cal->findChildren<QComboBox *>()) {
        cb->setFixedWidth(120);
    }
}
} // namespace

AiAnalyzeDialog::AiAnalyzeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AiAnalyzeDialog)
{
    // 初始化界面、样式与时间范围选择逻辑（相对区间 / 绝对时间点）。
    ui->setupUi(this);
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(false);

    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        const int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        const int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }

    ui->combo_unit->clear();
    ui->combo_unit->addItem(QStringLiteral("小时"), QStringLiteral("hour"));
    ui->combo_unit->addItem(QStringLiteral("天"), QStringLiteral("day"));
    ui->combo_unit->addItem(QStringLiteral("月"), QStringLiteral("month"));

    ui->line_value->setValidator(new QIntValidator(1, 999, ui->line_value));
    ui->line_value->setMaxLength(3);

    // 直接给滚动条本体套用聊天界面同款样式（灰色、圆角）。
    if (ui->edit_prompt && ui->edit_prompt->verticalScrollBar()) {
        ui->edit_prompt->verticalScrollBar()->setStyleSheet(chatScrollBarVerticalStyleSheet());
    }

    // ===== 时间范围模式：相对窗口 vs 选择具体时间点 =====
    ui->stack_rangeMode->setCurrentIndex(0); // 0=相对；1=绝对
    ui->but_modeToggle->setText(QStringLiteral("选择时间点"));

    // 绑定绝对时间点控件（来自 ui）
    m_dtStart = ui->dt_start;
    m_dtEnd = ui->dt_end;
    if (m_dtEnd) m_dtEnd->setDateTime(QDateTime::currentDateTime());
    if (m_dtStart && m_dtEnd) m_dtStart->setDateTime(m_dtEnd->dateTime().addDays(-1));

    // 注入自定义日历样式，避免首次弹出闪烁
    for (QDateTimeEdit *w : {m_dtStart, m_dtEnd}) {
        if (!w) continue;
        w->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
        w->setCalendarPopup(true);
        w->setStyleSheet(dtEditStyleSheet());

        // 使用自定义 QCalendarWidget，便于统一隐藏箭头/三角。
        QCalendarWidget *cal = new QCalendarWidget(w);
        setupCalendarWidget(cal);
        w->setCalendarWidget(cal);

        // 拦截键盘输入，只保留鼠标点击旋钮/日历选择。
        if (QLineEdit *le = w->findChild<QLineEdit *>()) {
            le->installEventFilter(this);
        }
        w->setCursor(QCursor(Qt::PointingHandCursor));
        w->setFocusPolicy(Qt::StrongFocus);
    }

    connect(ui->but_modeToggle, &QPushButton::clicked, this, [this]() {
        const int nextIdx = (ui->stack_rangeMode->currentIndex() == 0) ? 1 : 0;
        ui->stack_rangeMode->setCurrentIndex(nextIdx);
        ui->but_modeToggle->setText(nextIdx == 0 ? QStringLiteral("选择时间点") : QStringLiteral("返回相对模式"));
    });

    ui->but_confirm->setStyleSheet(BTN_ENABLE_STYLE);
}

AiAnalyzeDialog::~AiAnalyzeDialog()
{
    delete ui;
}

// 设置展示的好友名
void AiAnalyzeDialog::setPeerDisplayName(const QString &peerName)
{
    if (!ui || !ui->label_title) return;
    const QString name = peerName.trimmed();
    ui->label_title->setText(name.isEmpty()
                                 ? QStringLiteral("       AI 分析")
                                 : QStringLiteral("       AI 分析(%1)").arg(name));
}

// 时间范围数值（1～999），非法输入视为 0。
int AiAnalyzeDialog::rangeValue() const
{
    bool ok = false;
    const int v = ui->line_value->text().trimmed().toInt(&ok);
    return ok ? v : 0;
}

bool AiAnalyzeDialog::isAbsoluteTimeMode() const
{
    return ui && ui->stack_rangeMode && ui->stack_rangeMode->currentIndex() == 1;
}

// 绝对时间点模式下：读取开始时间。
QDateTime AiAnalyzeDialog::absoluteStartDateTime() const
{
    if (!m_dtStart) return {};
    return m_dtStart->dateTime();
}

// 绝对时间点模式下：读取结束时间。
QDateTime AiAnalyzeDialog::absoluteEndDateTime() const
{
    if (!m_dtEnd) return {};
    return m_dtEnd->dateTime();
}

// 当前选中的单位键：hour / day / month。
QString AiAnalyzeDialog::rangeUnitKey() const
{
    return ui->combo_unit->currentData().toString();
}

// 用户编辑的提示词
QString AiAnalyzeDialog::promptText() const
{
    return ui->edit_prompt->toPlainText().trimmed();
}

// 绘制无边框背景与外框。
void AiAnalyzeDialog::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPixmap background(":/pictures/094 Cloudy Apple - trans.png");
    if (background.isNull()) {
        qWarning("AiAnalyzeDialog: 背景图像加载失败");
        return;
    }
    const QSize newSize(this->width(), this->height());
    const QPixmap scaledBackground = background.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int x = (this->width() - scaledBackground.width()) / 2;
    const int y = (this->height() - scaledBackground.height()) / 2;
    const int radius = 10;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(x, y, scaledBackground);
    painter.setPen(QPen(QColor(255, 153, 179), 3.5));
    painter.drawRoundedRect(rect(), radius, radius);
}

// 拦截键盘输入，避免用户通过输入法直接修改时间；鼠标仍可通过日历/旋钮选择。
bool AiAnalyzeDialog::eventFilter(QObject *obj, QEvent *event)
{
    const QWidget *wObj = qobject_cast<const QWidget *>(obj);
    const bool withinStart = (m_dtStart && wObj) ? m_dtStart->isAncestorOf(wObj) : false;
    const bool withinEnd = (m_dtEnd && wObj) ? m_dtEnd->isAncestorOf(wObj) : false;
    const bool isTargetEditOrChild = (obj == m_dtStart || obj == m_dtEnd || withinStart || withinEnd);

    // 禁止通过键盘/快捷键修改时间；仍允许用鼠标通过内部控件（时间微调/日历选择）修改。
    if (isTargetEditOrChild
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        return true;
    }

    // 只在"点输入框的日期区域"时强制弹出日历，避免你点时间（时/分微调）时被干扰。
    if ((obj == m_dtStart || obj == m_dtEnd) && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        auto *w = static_cast<QWidget *>(obj);
        if (me && w) {
            const int x = me->position().toPoint().x();
            const int editW = w->width();
            if (editW > 0 && x < static_cast<int>(editW * 0.55)) {
                if (auto *edit = qobject_cast<QDateTimeEdit *>(obj)) {
                    if (QCalendarWidget *cal = edit->calendarWidget()) {
                        cal->raise();
                        cal->show();
                    }
                }
            }
        }
    }

    return QDialog::eventFilter(obj, event);
}

// 鼠标按下：判断是否进入拖动窗口状态。
void AiAnalyzeDialog::mousePressEvent(QMouseEvent *event)
{
    for (QWidget *widget : findChildren<QWidget *>()) {
        widget->clearFocus();
    }
    const QPoint pos = event->pos();
    if (pos.x() <= 30 || pos.x() >= width() - 30 || pos.y() <= 30 || pos.y() >= height() - 30) {
        m_moveFlag = 1;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

// 鼠标移动：在拖动状态下移动窗口。
void AiAnalyzeDialog::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint globalPos = event->globalPosition().toPoint();
    if (event->buttons() & Qt::LeftButton) {
        if (m_moveFlag == 1) {
            move(globalPos - m_dragPosition);
            event->accept();
        }
        m_dragPosition = globalPos - frameGeometry().topLeft();
    }
    QDialog::mouseMoveEvent(event);
}

// 鼠标松开：退出拖动状态。
void AiAnalyzeDialog::mouseReleaseEvent(QMouseEvent *event)
{
    QDialog::mouseReleaseEvent(event);
    m_moveFlag = 0;
}

void AiAnalyzeDialog::on_but_deletewindow_clicked()
{
    reject();
}

// 确认按钮：校验输入并接受对话框。
void AiAnalyzeDialog::on_but_confirm_clicked()
{
    if (isAbsoluteTimeMode()) {
        const QDateTime st = absoluteStartDateTime();
        const QDateTime en = absoluteEndDateTime();
        if (!st.isValid() || !en.isValid()) {
            return;
        }
        if (st > en) {
            return;
        }
    } else {
        const int rv = rangeValue();
        if (rv < 1 || rv > 999) {
            return;
        }
        if (rangeUnitKey().isEmpty()) {
            return;
        }
    }
    accept();
}
