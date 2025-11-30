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
        start = end;  // 从块尾开始
        break;

    case SelectionMode::Character:
    default:
        start = end = charPos;
        break;
    }

    setSelectionRange(pageIndex, start, end, mode);

    // 设置锚点
    m_anchorPos = start;
    m_hasAnchor = true;
}

void TextSelector::updateSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_isSelecting || m_startPageIndex < 0) {
        return;
    }

    // 暂时只支持同一页内选择
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
        // 单词模式：找到当前位置的单词边界
        CharPosition wordStart, wordEnd;
        findWordBoundary(pageData, currentPos, &wordStart, &wordEnd);

        // 与初始单词比较，扩展选择
        if (currentPos < m_startCharPos) {
            start = wordStart;
            end = m_wordEnd;
        } else {
            start = m_wordStart;
            end = wordEnd;
        }
    }
    else if (m_selection.mode == SelectionMode::Line) {
        // 行模式：找到当前位置的行边界
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
        // 字符模式
        start = m_startCharPos;
        end = currentPos;
    }

    setSelectionRange(pageIndex, start, end, m_selection.mode);
}

void TextSelector::extendSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (!m_hasAnchor) {
        // 如果没有锚点，创建新选择
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

    // 设置锚点为单词开始
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

    // 找到第一个和最后一个字符
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
    m_isSelecting = false;
    m_hasAnchor = false;
    emit selectionChanged();
}

void TextSelector::copyToClipboard()
{
    if (!hasSelection()) {
        return;
    }

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(m_selection.selectedText);

    qDebug() << "Copied to clipboard:" << m_selection.selectedText.length() << "characters";
}

CharPosition TextSelector::hitTestCharacter(const PageTextData& pageData,
                                            const QPointF& pos,
                                            double zoom)
{
    // 将屏幕坐标转换为页面坐标（去除缩放）
    QPointF pageCoord(pos.x() / zoom, pos.y() / zoom);

    double minDistance = std::numeric_limits<double>::max();
    CharPosition result;

    for (int b = 0; b < pageData.blocks.size(); ++b) {
        const TextBlock& block = pageData.blocks[b];

        for (int l = 0; l < block.lines.size(); ++l) {
            const TextLine& line = block.lines[l];

            if (line.chars.isEmpty()) continue;

            // 检查是否在行的垂直范围内（扩大容差到50%）
            double lineTop = line.bbox.top();
            double lineBottom = line.bbox.bottom();
            double verticalMargin = line.bbox.height() * 0.5;

            // 如果不在行的垂直范围内，跳过
            if (pageCoord.y() < lineTop - verticalMargin ||
                pageCoord.y() > lineBottom + verticalMargin) {
                continue;
            }

            // 在行的水平范围内查找最近的字符
            for (int c = 0; c < line.chars.size(); ++c) {
                const TextChar& ch = line.chars[c];

                // 如果点在字符bbox内，直接返回
                if (ch.bbox.contains(pageCoord)) {
                    return CharPosition(b, l, c);
                }

                // 计算到字符中心的距离
                QPointF charCenter = ch.bbox.center();
                double distance = QLineF(pageCoord, charCenter).length();

                if (distance < minDistance) {
                    minDistance = distance;
                    result = CharPosition(b, l, c);
                }
            }

            // 如果点在行内但超过最后一个字符，选择最后一个字符
            if (pageCoord.y() >= lineTop && pageCoord.y() <= lineBottom) {
                if (pageCoord.x() > line.chars.last().bbox.right()) {
                    int lastCharIdx = line.chars.size() - 1;
                    double distance = pageCoord.x() - line.chars.last().bbox.right();
                    if (distance < minDistance) {
                        minDistance = distance;
                        result = CharPosition(b, l, lastCharIdx);
                    }
                }
                // 如果点在第一个字符之前，选择第一个字符
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
    return (u >= 0x4E00 && u <= 0x9FFF) ||    // CJK Unified Ideographs
           (u >= 0x3400 && u <= 0x4DBF) ||    // CJK Extension A
           (u >= 0xF900 && u <= 0xFAFF) ||    // CJK Compatibility Ideographs
           (u >= 0x3040 && u <= 0x30FF) ||    // Japanese Hiragana/Katakana
           (u >= 0xAC00 && u <= 0xD7AF);      // Korean Hangul
}

void TextSelector::findWordBoundary(
    const PageTextData& pageData,
    const CharPosition& pos,
    CharPosition* start,
    CharPosition* end)
{
    const TextLine& line = pageData.blocks[pos.blockIndex].lines[pos.lineIndex];
    QChar c = line.chars[pos.charIndex].character;

    // ★★★ 如果是中文/日文/韩文：单字为“词” ★★★
    if (isCJK(c)) {
        *start = pos;
        *end = pos;
        return;
    }

    // 原英文处理逻辑
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

    // 块的开始：第一行第一个字符
    *start = CharPosition(pos.blockIndex, 0, 0);

    // 块的结束：最后一行最后一个字符
    const TextLine& lastLine = block.lines.last();
    *end = CharPosition(pos.blockIndex, block.lines.size() - 1,
                        lastLine.chars.size() - 1);
}

bool TextSelector::isWordSeparator(QChar ch) const
{
    // 空格、标点、换行等都是单词分隔符
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

    // 确保start在end之前
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

            // 行尾添加换行符（除了最后一个字符）
            if (b != endBlock || l != endLine) {
                text.append('\n');
            }
        }

        // 块之间添加额外换行
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

            // 合并同一行的字符bbox
            QRectF lineRect = line.chars[firstChar].bbox;
            for (int c = firstChar + 1; c <= lastChar && c < line.chars.size(); ++c) {
                lineRect = lineRect.united(line.chars[c].bbox);
            }

            rects.append(lineRect);
        }
    }

    return rects;
}
