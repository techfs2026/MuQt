#ifndef TEXTSELECTOR_H
#define TEXTSELECTOR_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QTimer>
#include "textcachemanager.h"

class PerThreadMuPDFRenderer;
class TextCacheManager;

enum class SelectionMode {
    Character,
    Word,
    Line,
    Block
};

struct TextSelection {
    int pageIndex;
    int startBlockIndex;
    int startLineIndex;
    int startCharIndex;
    int endBlockIndex;
    int endLineIndex;
    int endCharIndex;

    SelectionMode mode;

    QVector<QRectF> highlightRects;
    QString selectedText;

    TextSelection()
        : pageIndex(-1)
        , startBlockIndex(-1)
        , startLineIndex(-1)
        , startCharIndex(-1)
        , endBlockIndex(-1)
        , endLineIndex(-1)
        , endCharIndex(-1)
        , mode(SelectionMode::Character)
    {}

    bool isValid() const {
        return pageIndex >= 0 && startCharIndex >= 0 && endCharIndex >= 0;
    }

    void clear() {
        pageIndex = -1;
        startBlockIndex = startLineIndex = startCharIndex = -1;
        endBlockIndex = endLineIndex = endCharIndex = -1;
        highlightRects.clear();
        selectedText.clear();
        mode = SelectionMode::Character;
    }
};

struct CharPosition {
    int blockIndex;
    int lineIndex;
    int charIndex;

    CharPosition() : blockIndex(-1), lineIndex(-1), charIndex(-1) {}
    CharPosition(int b, int l, int c) : blockIndex(b), lineIndex(l), charIndex(c) {}

    bool isValid() const { return blockIndex >= 0 && lineIndex >= 0 && charIndex >= 0; }

    bool operator<(const CharPosition& other) const {
        if (blockIndex != other.blockIndex) return blockIndex < other.blockIndex;
        if (lineIndex != other.lineIndex) return lineIndex < other.lineIndex;
        return charIndex < other.charIndex;
    }

    bool operator==(const CharPosition& other) const {
        return blockIndex == other.blockIndex &&
               lineIndex == other.lineIndex &&
               charIndex == other.charIndex;
    }
};

class TextSelector : public QObject
{
    Q_OBJECT

public:
    explicit TextSelector(PerThreadMuPDFRenderer* renderer,
                          TextCacheManager* textCache,
                          QObject* parent = nullptr);

    void startSelection(int pageIndex, const QPointF& pagePos, double zoom,
                        SelectionMode mode = SelectionMode::Character);

    void updateSelection(int pageIndex, const QPointF& pagePos, double zoom);

    void extendSelection(int pageIndex, const QPointF& pagePos, double zoom);

    void endSelection();

    void clearSelection();

    void selectLine(int pageIndex, const QPointF& pagePos, double zoom);

    void selectBlock(int pageIndex, const QPointF& pagePos, double zoom);

    void selectWord(int pageIndex, const QPointF& pagePos, double zoom);

    void selectAll(int pageIndex);

    const TextSelection& currentSelection() const { return m_selection; }

    bool hasSelection() const { return m_selection.isValid(); }

    QString selectedText() const { return m_selection.selectedText; }

    void copyToClipboard();

    bool isSelecting() const { return m_isSelecting; }

signals:
    void selectionChanged();

    void scrollRequested(int direction);

private:
    CharPosition hitTestCharacter(const PageTextData& pageData,
                                  const QPointF& pos,
                                  double zoom);

    void findWordBoundary(const PageTextData& pageData,
                          const CharPosition& pos,
                          CharPosition* start,
                          CharPosition* end);

    void findLineBoundary(const PageTextData& pageData,
                          const CharPosition& pos,
                          CharPosition* start,
                          CharPosition* end);

    void findBlockBoundary(const PageTextData& pageData,
                           const CharPosition& pos,
                           CharPosition* start,
                           CharPosition* end);

    bool isWordSeparator(QChar ch) const;

    void setSelectionRange(int pageIndex,
                           const CharPosition& start,
                           const CharPosition& end,
                           SelectionMode mode = SelectionMode::Character);

    void buildSelection();

    QString extractSelectedText(const PageTextData& pageData);

    QVector<QRectF> calculateHighlightRects(const PageTextData& pageData);

private:
    PerThreadMuPDFRenderer* m_renderer;
    TextCacheManager* m_textCache;

    TextSelection m_selection;
    bool m_isSelecting;

    CharPosition m_anchorPos;
    bool m_hasAnchor;

    int m_startPageIndex;
    CharPosition m_startCharPos;
    double m_startZoom;

    CharPosition m_wordStart;
    CharPosition m_wordEnd;
};

#endif // TEXTSELECTOR_H
