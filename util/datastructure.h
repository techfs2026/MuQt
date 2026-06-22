#ifndef DATASTRUCTURE_H
#define DATASTRUCTURE_H

#include <QChar>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QImage>
#include <QMetaType>

struct RenderResult {
    bool success = false;
    QImage image;
    QString errorMessage;
};

struct ViewportRestoreState {
    int pageIndex;
    double pageOffsetRatio;
    bool needRestore;

    ViewportRestoreState()
        : pageIndex(-1), pageOffsetRatio(0.0), needRestore(false) {}

    void reset() {
        pageIndex = -1;
        pageOffsetRatio = 0.0;
        needRestore = false;
    }
};


enum class ZoomMode {
    Custom,
    FitWidth,
    FitPage
};


enum class PageDisplayMode {
    SinglePage,
    DoublePage
};


struct TextChar {
    QChar character;
    QRectF bbox;
};

struct TextLine {
    QVector<TextChar> chars;
    QRectF bbox;
};

struct TextBlock {
    QVector<TextLine> lines;
    QRectF bbox;
};

struct PageTextData {
    int pageIndex;
    QVector<TextBlock> blocks;
    QString fullText;

    PageTextData() : pageIndex(-1) {}
    bool isEmpty() const { return blocks.isEmpty(); }
    bool isValid() const { return pageIndex >= 0; }
};

Q_DECLARE_METATYPE(PageTextData)

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWords = false;
    int maxResults = 1000;
};


struct SearchResult {
    int pageIndex;
    QVector<QRectF> quads;
    QString context;

    SearchResult() : pageIndex(-1) {}
    explicit SearchResult(int page) : pageIndex(page) {}

    bool isValid() const { return pageIndex >= 0 && !quads.isEmpty(); }
};

#endif // DATASTRUCTURE_H
