#pragma once

#include <QString>

/**
 * @file ChatScrollStyle.h
 * 与会话区一致的 QPlainTextEdit 垂直滚动条样式字符串。
 */

// 与 MainWindow.ui 中 stack_talks（TalkStacked）内 QListWidget 区域使用的垂直滚动条样式一致，
// 供 QPlainTextEdit 通过子控件选择器套用。
inline QString chatPlainTextEditVerticalScrollStyleSheet()
{
    return QStringLiteral(
        "\n"
        "QPlainTextEdit QScrollBar:vertical {"
        "    width: 12px;"
        "    background: #f0f0f0;"
        "    border-radius: 6px;"
        "    margin: 0px;"
        "    padding: 0px;"
        "}"
        "QPlainTextEdit QScrollBar::handle:vertical {"
        "    background: #888;"
        "    min-height: 40px;"
        "    border-radius: 6px;"
        "    margin: 0px;"
        "}"
        "QPlainTextEdit QScrollBar::handle:vertical:hover {"
        "    background: #555;"
        "}"
        "QPlainTextEdit QScrollBar::add-line:vertical, QPlainTextEdit QScrollBar::sub-line:vertical {"
        "    height: 0px;"
        "    background: none;"
        "}"
        "QPlainTextEdit QScrollBar::add-page:vertical, QPlainTextEdit QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"
        "\n");
}

// 直接对所有竖直 QScrollBar 生效。
inline QString chatScrollBarVerticalStyleSheet()
{
    return QStringLiteral(
        "\n"
        "QScrollBar:vertical {"
        "    width: 12px;"
        "    background: #f0f0f0;"
        "    border-radius: 6px;"
        "    margin: 0px;"
        "    padding: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #888;"
        "    min-height: 40px;"
        "    border-radius: 6px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #555;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    height: 0px;"
        "    background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"
        "\n");
}

