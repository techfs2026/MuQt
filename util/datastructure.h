#ifndef ENUMS_H
#define ENUMS_H

#include <QChar>
#include <QRectF>
#include <QString>
#include <QVector>

// 缩放模式
enum class ZoomMode {
    Custom,
    FitWidth,
    FitPage
};

// 页面显示模式
enum class PageDisplayMode {
    SinglePage,
    DoublePage
};

// 文本字符及其位置信息
struct TextChar {
    QChar character;
    QRectF bbox;  // 字符的边界框
};

// 文本行
struct TextLine {
    QVector<TextChar> chars;
    QRectF bbox;  // 行的边界框
};

// 文本块
struct TextBlock {
    QVector<TextLine> lines;
    QRectF bbox;  // 块的边界框
};

// 页面的完整文本信息（纯数据，不包含 MuPDF 对象）
struct PageTextData {
    int pageIndex;
    QVector<TextBlock> blocks;
    QString fullText;  // 完整文本（用于快速搜索）

    PageTextData() : pageIndex(-1) {}
    bool isEmpty() const { return blocks.isEmpty(); }
    bool isValid() const { return pageIndex >= 0; }
};

#endif // ENUMS_H
