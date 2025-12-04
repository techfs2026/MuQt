#ifndef DATASTRUCTURE_H
#define DATASTRUCTURE_H

#include <QChar>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QImage>

struct RenderResult {
    bool success = false;
    QImage image;
    QString errorMessage;
};

struct ViewportRestoreState {
    int pageIndex;
    double pageOffsetRatio;  // 页面内的垂直偏移百分比 [0.0, 1.0]
    bool needRestore;

    ViewportRestoreState()
        : pageIndex(-1), pageOffsetRatio(0.0), needRestore(false) {}

    void reset() {
        pageIndex = -1;
        pageOffsetRatio = 0.0;
        needRestore = false;
    }
};

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

// ========== 搜索选项 ==========

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWords = false;
    int maxResults = 1000;
};

// ========== 搜索结果 ==========

struct SearchResult {
    int pageIndex;
    QVector<QRectF> quads;  // 匹配文本的位置
    QString context;        // 上下文

    SearchResult() : pageIndex(-1) {}
    explicit SearchResult(int page) : pageIndex(page) {}

    bool isValid() const { return pageIndex >= 0 && !quads.isEmpty(); }
};

#endif // DATASTRUCTURE_H
