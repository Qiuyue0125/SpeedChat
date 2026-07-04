#pragma once

#include <QIcon>

class QPushButton;

/**
 * 关闭按钮工具：统一样式、图标、hover 切换。
 * 不属于配置文件逻辑，独立的 UI 工具。
 */
namespace CloseButtonUtils {

// 获取关闭按钮统一样式表（跨平台，26×26，透明底，hover 红底）。
const char *styleSheet();

// 一键配置关闭按钮：样式 + 图标 + hover 图标切换（通过 eventFilter）。
// 参数 hoverIcon 若 isNull() 则 hover 时仍保持原图标。
void setup(QPushButton *btn, const QIcon &hoverIcon = QIcon());

} // namespace CloseButtonUtils