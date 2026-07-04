/**
 * @file CloseButtonUtils.cpp
 * 关闭按钮统一样式与 hover 图标切换。
 */
#include "CloseButtonUtils.h"

#include <QPushButton>
#include <QEvent>

namespace CloseButtonUtils {

namespace {

// 关闭按钮 hover 图标切换 filter。
class CloseBtnFilter : public QObject
{
public:
    CloseBtnFilter(const QIcon &normal, const QIcon &hover, QPushButton *btn)
        : QObject(btn), m_normal(normal), m_hover(hover), m_btn(btn) {}

protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (!m_btn || obj != m_btn) return QObject::eventFilter(obj, e);
        if (e->type() == QEvent::Enter) {
            if (!m_hover.isNull()) m_btn->setIcon(m_hover);
        } else if (e->type() == QEvent::Leave) {
            m_btn->setIcon(m_normal);
        }
        return QObject::eventFilter(obj, e);
    }

private:
    QIcon m_normal;
    QIcon m_hover;
    QPushButton *m_btn;
};

} // namespace

const char *styleSheet()
{
    return R"(
QPushButton {
    border: none;
    background: transparent;
    padding: 0px;
    margin: 0px;
    min-width: 26px;
    min-height: 26px;
    max-width: 26px;
    max-height: 26px;
}
QPushButton:hover {
    border: none;
    background: red;
    border-radius: 4px;
}
)";
}

void setup(QPushButton *btn, const QIcon &hoverIcon)
{
    if (!btn) return;
    btn->setStyleSheet(styleSheet());
    const QIcon normal(QStringLiteral(":/pictures/icon_close_normal.png"));
    btn->setIcon(normal);
    btn->setIconSize(QSize(12, 12));
    btn->installEventFilter(new CloseBtnFilter(normal, hoverIcon.isNull() ? normal : hoverIcon, btn));
}

} // namespace CloseButtonUtils