#include "textselector.h"
#include "perthreadmupdfrenderer.h"
#include "textcachemanager.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>

TextSelector::TextSelector(PerThreadMuPDFRenderer* renderer,
                           TextCacheManager* textCache,
                           QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_textCache(textCache)
    , m_isSelecting(false)
    , m_hasAnchor(false)
    , m_startPageIndex(-1)
    , m_startZoom(1.0)
{
}

void TextSelector::startSelection(int pageIndex, const QPointF& pagePos, double zoom,
                                  SelectionMode mode)
{
    if (!m_renderer || !m_textCache) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition charPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!charPos.isValid()) {
        return;
    }

    m_isSelecting = true;
    m_startPageIndex = pageIndex;
    m_startCharPos = charPos;
    m_startZoom = zoom;

    // 单击（Character）只落锚点、不高亮单个字符：真正的选区在拖动时由
    // updateSelection 用 m_startCharPos→当前点建立。双击词/三击行/Shift 扩展不变。
    if (mode == SelectionMode::Character) {
        m_selection.clear();
        m_anchorPos = charPos;
        m_hasAnchor = true;
        emit selectionChanged();
        return;
    }

    CharPosition start, end;

    switch (mode) {
    case SelectionMode::Word:
        findWordBoundary(pageData, charPos, &start, &end);
        m_wordStart = start;
        m_wordEnd = end;
        break;

    case SelectionMode::Line:
        findLineBoundary(pageData, charPos, &start, &end);
        m_wordStart = start;
        m_wordEnd = end;
        break;

    case SelectionMode::Block:
        findBlockBoundary(pageData, charPos, &start, &end);
        start = end;
        break;

    case SelectionMode::Character:
    default:
        start = end = charPos;
        break;
    }

    setSelectionRange(pageIndex, start, end, mode);

    m_anchorPos = start;
    m_hasAnchor = true;
}

void TextSelector::updateSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_isSelecting || m_startPageIndex < 0) {
        return;
    }

    if (pageIndex != m_startPageIndex) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition currentPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!currentPos.isValid()) {
        return;
    }

    CharPosition start, end;

    if (m_selection.mode == SelectionMode::Word) {
        CharPosition wordStart, wordEnd;
        findWordBoundary(pageData, currentPos, &wordStart, &wordEnd);

        if (currentPos < m_startCharPos) {
            start = wordStart;
            end = m_wordEnd;
        } else {
            start = m_wordStart;
            end = wordEnd;
        }
    }
    else if (m_selection.mode == SelectionMode::Line) {
        CharPosition lineStart, lineEnd;
        findLineBoundary(pageData, currentPos, &lineStart, &lineEnd);

        if (currentPos < m_startCharPos) {
            start = lineStart;
            end = m_wordEnd;
        } else {
            start = m_wordStart;
            end = lineEnd;
        }
    }
    else {
        start = m_startCharPos;
        end = currentPos;
    }

    setSelectionRange(pageIndex, start, end, m_selection.mode);
}

void TextSelector::extendSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_hasAnchor) {
        startSelection(pageIndex, pagePos, zoom, SelectionMode::Character);
        return;
    }

    if (pageIndex != m_selection.pageIndex) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition endPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!endPos.isValid()) {
        return;
    }

    setSelectionRange(pageIndex, m_anchorPos, endPos, SelectionMode::Character);
}

void TextSelector::selectWord(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_renderer || !m_textCache) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition charPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!charPos.isValid()) {
        return;
    }

    CharPosition start, end;
    findWordBoundary(pageData, charPos, &start, &end);

    setSelectionRange(pageIndex, start, end, SelectionMode::Word);

    m_anchorPos = start;
    m_hasAnchor = true;
}

void TextSelector::selectLine(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_renderer || !m_textCache) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition charPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!charPos.isValid()) {
        return;
    }

    CharPosition start, end;
    findLineBoundary(pageData, charPos, &start, &end);

    setSelectionRange(pageIndex, start, end, SelectionMode::Line);

    m_anchorPos = start;
    m_hasAnchor = true;
}

void TextSelector::selectBlock(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_renderer || !m_textCache) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    CharPosition charPos = hitTestCharacter(pageData, pagePos, zoom);
    if (!charPos.isValid()) {
        return;
    }

    CharPosition start, end;
    findBlockBoundary(pageData, charPos, &start, &end);

    setSelectionRange(pageIndex, start, end, SelectionMode::Block);

    m_anchorPos = start;
    m_hasAnchor = true;
}

void TextSelector::selectAll(int pageIndex)
{
    if (!m_renderer || !m_textCache) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(pageIndex);
    if (!pageData.isValid() || pageData.blocks.isEmpty()) {
        return;
    }

    CharPosition start(0, 0, 0);

    const TextBlock& lastBlock = pageData.blocks.last();
    const TextLine& lastLine = lastBlock.lines.last();
    CharPosition end(pageData.blocks.size() - 1,
                     lastBlock.lines.size() - 1,
                     lastLine.chars.size() - 1);

    setSelectionRange(pageIndex, start, end, SelectionMode::Character);
}

void TextSelector::endSelection()
{
    m_isSelecting = false;

    if (m_selection.isValid()) {
        buildSelection();
    }
}

void TextSelector::clearSelection()
{
    m_selection.clear();
    m_hasAnchor = false;
    m_isSelecting = false;
    emit selectionChanged();
}

void TextSelector::copyToClipboard()
{
    if (!m_selection.isValid()) {
        return;
    }

    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(m_selection.selectedText);
    }
}

CharPosition TextSelector::hitTestCharacter(const PageTextData& pageData,
                                            const QPointF& pos,
                                            double zoom)
{
    if (!pageData.isValid() || pageData.blocks.isEmpty()) {
        return CharPosition();
    }

    QPointF pageCoord = pos / zoom;

    CharPosition result;
    double minDistance = 1e9;

    for (int b = 0; b < pageData.blocks.size(); ++b) {
        const TextBlock& block = pageData.blocks[b];

        for (int l = 0; l < block.lines.size(); ++l) {
            const TextLine& line = block.lines[l];
            if (line.chars.isEmpty()) continue;

            double lineTop = line.bbox.top();
            double lineBottom = line.bbox.bottom();

            for (int c = 0; c < line.chars.size(); ++c) {
                const TextChar& ch = line.chars[c];

                if (ch.bbox.contains(pageCoord)) {
                    return CharPosition(b, l, c);
                }

                QPointF charCenter = ch.bbox.center();
                double distance = QLineF(pageCoord, charCenter).length();

                if (distance < minDistance) {
                    minDistance = distance;
                    result = CharPosition(b, l, c);
                }
            }

            if (pageCoord.y() >= lineTop && pageCoord.y() <= lineBottom) {
                if (pageCoord.x() > line.chars.last().bbox.right()) {
                    int lastCharIdx = line.chars.size() - 1;
                    double distance = pageCoord.x() - line.chars.last().bbox.right();
                    if (distance < minDistance) {
                        minDistance = distance;
                        result = CharPosition(b, l, lastCharIdx);
                    }
                }
                else if (pageCoord.x() < line.chars.first().bbox.left()) {
                    double distance = line.chars.first().bbox.left() - pageCoord.x();
                    if (distance < minDistance) {
                        minDistance = distance;
                        result = CharPosition(b, l, 0);
                    }
                }
            }
        }
    }

    return result;
}

static inline bool isCJK(QChar ch)
{
    uint u = ch.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF) ||
           (u >= 0x3400 && u <= 0x4DBF) ||
           (u >= 0xF900 && u <= 0xFAFF) ||
           (u >= 0x3040 && u <= 0x30FF) ||
           (u >= 0xAC00 && u <= 0xD7AF);
}

void TextSelector::findWordBoundary(
    const PageTextData& pageData,
    const CharPosition& pos,
    CharPosition* start,
    CharPosition* end)
{
    const TextLine& line = pageData.blocks[pos.blockIndex].lines[pos.lineIndex];
    QChar c = line.chars[pos.charIndex].character;

    if (isCJK(c)) {
        *start = pos;
        *end = pos;
        return;
    }

    int startIdx = pos.charIndex;
    while (startIdx > 0) {
        QChar prev = line.chars[startIdx - 1].character;
        if (isWordSeparator(prev)) break;
        startIdx--;
    }

    int endIdx = pos.charIndex;
    while (endIdx < line.chars.size() - 1) {
        QChar next = line.chars[endIdx + 1].character;
        if (isWordSeparator(next)) break;
        endIdx++;
    }

    *start = CharPosition(pos.blockIndex, pos.lineIndex, startIdx);
    *end   = CharPosition(pos.blockIndex, pos.lineIndex, endIdx);
}


void TextSelector::findLineBoundary(const PageTextData& pageData,
                                    const CharPosition& pos,
                                    CharPosition* start,
                                    CharPosition* end)
{
    if (!pos.isValid() || pos.blockIndex >= pageData.blocks.size()) {
        return;
    }

    const TextBlock& block = pageData.blocks[pos.blockIndex];
    if (pos.lineIndex >= block.lines.size()) {
        return;
    }

    const TextLine& line = block.lines[pos.lineIndex];
    if (line.chars.isEmpty()) {
        return;
    }

    *start = CharPosition(pos.blockIndex, pos.lineIndex, 0);
    *end = CharPosition(pos.blockIndex, pos.lineIndex, line.chars.size() - 1);
}

void TextSelector::findBlockBoundary(const PageTextData& pageData,
                                     const CharPosition& pos,
                                     CharPosition* start,
                                     CharPosition* end)
{
    if (!pos.isValid() || pos.blockIndex >= pageData.blocks.size()) {
        return;
    }

    const TextBlock& block = pageData.blocks[pos.blockIndex];
    if (block.lines.isEmpty()) {
        return;
    }

    *start = CharPosition(pos.blockIndex, 0, 0);

    const TextLine& lastLine = block.lines.last();
    *end = CharPosition(pos.blockIndex, block.lines.size() - 1,
                        lastLine.chars.size() - 1);
}

bool TextSelector::isWordSeparator(QChar ch) const
{
    return ch.isSpace() || ch.isPunct() ||
           ch == '\n' || ch == '\r' || ch == '\t' ||
           ch.category() == QChar::Separator_Space ||
           ch.category() == QChar::Separator_Line ||
           ch.category() == QChar::Separator_Paragraph;
}

void TextSelector::setSelectionRange(int pageIndex,
                                     const CharPosition& start,
                                     const CharPosition& end,
                                     SelectionMode mode)
{
    m_selection.pageIndex = pageIndex;
    m_selection.mode = mode;

    if (end < start) {
        m_selection.startBlockIndex = end.blockIndex;
        m_selection.startLineIndex = end.lineIndex;
        m_selection.startCharIndex = end.charIndex;
        m_selection.endBlockIndex = start.blockIndex;
        m_selection.endLineIndex = start.lineIndex;
        m_selection.endCharIndex = start.charIndex;
    } else {
        m_selection.startBlockIndex = start.blockIndex;
        m_selection.startLineIndex = start.lineIndex;
        m_selection.startCharIndex = start.charIndex;
        m_selection.endBlockIndex = end.blockIndex;
        m_selection.endLineIndex = end.lineIndex;
        m_selection.endCharIndex = end.charIndex;
    }

    buildSelection();
    emit selectionChanged();
}

void TextSelector::buildSelection()
{
    if (!m_selection.isValid()) {
        return;
    }

    PageTextData pageData = m_textCache->getPageTextData(m_selection.pageIndex);
    if (!pageData.isValid()) {
        return;
    }

    m_selection.selectedText = extractSelectedText(pageData);
    m_selection.highlightRects = calculateHighlightRects(pageData);
}

QString TextSelector::extractSelectedText(const PageTextData& pageData)
{
    QString text;

    int startBlock = m_selection.startBlockIndex;
    int startLine = m_selection.startLineIndex;
    int startChar = m_selection.startCharIndex;
    int endBlock = m_selection.endBlockIndex;
    int endLine = m_selection.endLineIndex;
    int endChar = m_selection.endCharIndex;

    for (int b = startBlock; b <= endBlock && b < pageData.blocks.size(); ++b) {
        const TextBlock& block = pageData.blocks[b];

        int firstLine = (b == startBlock) ? startLine : 0;
        int lastLine = (b == endBlock) ? endLine : block.lines.size() - 1;

        for (int l = firstLine; l <= lastLine && l < block.lines.size(); ++l) {
            const TextLine& line = block.lines[l];

            int firstChar = (b == startBlock && l == startLine) ? startChar : 0;
            int lastChar = (b == endBlock && l == endLine) ? endChar : line.chars.size() - 1;

            for (int c = firstChar; c <= lastChar && c < line.chars.size(); ++c) {
                text.append(line.chars[c].character);
            }

            if (b != endBlock || l != endLine) {
                text.append('\n');
            }
        }

        if (b != endBlock) {
            text.append('\n');
        }
    }

    return text;
}

QVector<QRectF> TextSelector::calculateHighlightRects(const PageTextData& pageData)
{
    QVector<QRectF> rects;

    int startBlock = m_selection.startBlockIndex;
    int startLine = m_selection.startLineIndex;
    int startChar = m_selection.startCharIndex;
    int endBlock = m_selection.endBlockIndex;
    int endLine = m_selection.endLineIndex;
    int endChar = m_selection.endCharIndex;

    for (int b = startBlock; b <= endBlock && b < pageData.blocks.size(); ++b) {
        const TextBlock& block = pageData.blocks[b];

        int firstLine = (b == startBlock) ? startLine : 0;
        int lastLine = (b == endBlock) ? endLine : block.lines.size() - 1;

        for (int l = firstLine; l <= lastLine && l < block.lines.size(); ++l) {
            const TextLine& line = block.lines[l];

            if (line.chars.isEmpty()) continue;

            int firstChar = (b == startBlock && l == startLine) ? startChar : 0;
            int lastChar = (b == endBlock && l == endLine) ? endChar : line.chars.size() - 1;

            if (firstChar >= line.chars.size() || lastChar >= line.chars.size()) {
                continue;
            }

            QRectF lineRect = line.chars[firstChar].bbox;
            for (int c = firstChar + 1; c <= lastChar && c < line.chars.size(); ++c) {
                lineRect = lineRect.united(line.chars[c].bbox);
            }

            rects.append(lineRect);
        }
    }

    return rects;
}
