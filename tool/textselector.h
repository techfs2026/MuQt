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

/**
 * @brief 选择模式
 */
enum class SelectionMode {
    Character,      ///< 字符级别选择
    Word,           ///< 单词级别选择（双击）
    Line,           ///< 行级别选择（三击）
    Block           ///< 块级别选择
};

/**
 * @brief 文本选择范围
 */
struct TextSelection {
    int pageIndex;                          ///< 页码
    int startBlockIndex;                    ///< 起始块索引
    int startLineIndex;                     ///< 起始行索引
    int startCharIndex;                     ///< 起始字符索引
    int endBlockIndex;                      ///< 结束块索引
    int endLineIndex;                       ///< 结束行索引
    int endCharIndex;                       ///< 结束字符索引

    SelectionMode mode;                     ///< 选择模式

    QVector<QRectF> highlightRects;         ///< 高亮矩形列表（页面坐标）
    QString selectedText;                   ///< 选中的文本

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

/**
 * @brief 字符位置信息
 */
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

/**
 * @brief 文本选择器（增强版 - Word/网页风格）
 */
class TextSelector : public QObject
{
    Q_OBJECT

public:
    explicit TextSelector(PerThreadMuPDFRenderer* renderer,
                          TextCacheManager* textCache,
                          QObject* parent = nullptr);

    /**
     * @brief 开始文本选择
     * @param pageIndex 页码
     * @param pagePos 页面坐标（已考虑缩放）
     * @param zoom 当前缩放比例
     * @param mode 选择模式
     */
    void startSelection(int pageIndex, const QPointF& pagePos, double zoom,
                        SelectionMode mode = SelectionMode::Character);

    /**
     * @brief 更新选择范围
     * @param pageIndex 页码
     * @param pagePos 页面坐标（已考虑缩放）
     * @param zoom 当前缩放比例
     */
    void updateSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 扩展选择（Shift+点击）
     * @param pageIndex 页码
     * @param pagePos 页面坐标
     * @param zoom 缩放比例
     */
    void extendSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 结束选择
     */
    void endSelection();

    /**
     * @brief 清除选择
     */
    void clearSelection();

    /**
     * @brief 选择整行
     */
    void selectLine(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 选择整个块/段落
     */
    void selectBlock(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 选择单词
     */
    void selectWord(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 全选当前页
     */
    void selectAll(int pageIndex);

    /**
     * @brief 获取当前选择
     */
    const TextSelection& currentSelection() const { return m_selection; }

    /**
     * @brief 是否有选中的文本
     */
    bool hasSelection() const { return m_selection.isValid(); }

    /**
     * @brief 获取选中的文本
     */
    QString selectedText() const { return m_selection.selectedText; }

    /**
     * @brief 复制选中的文本到剪贴板
     */
    void copyToClipboard();

    /**
     * @brief 是否正在选择
     */
    bool isSelecting() const { return m_isSelecting; }

signals:
    /**
     * @brief 选择改变信号
     */
    void selectionChanged();

    /**
     * @brief 需要滚动信号（拖拽选择超出可见区域时）
     * @param direction 1=向下，-1=向上
     */
    void scrollRequested(int direction);

private:
    /**
     * @brief 命中测试：找到位置对应的字符
     */
    CharPosition hitTestCharacter(const PageTextData& pageData,
                                  const QPointF& pos,
                                  double zoom);

    /**
     * @brief 查找单词边界
     * @param pageData 页面数据
     * @param pos 起始位置
     * @param start 输出单词起始位置
     * @param end 输出单词结束位置
     */
    void findWordBoundary(const PageTextData& pageData,
                          const CharPosition& pos,
                          CharPosition* start,
                          CharPosition* end);

    /**
     * @brief 查找行边界
     */
    void findLineBoundary(const PageTextData& pageData,
                          const CharPosition& pos,
                          CharPosition* start,
                          CharPosition* end);

    /**
     * @brief 查找块边界
     */
    void findBlockBoundary(const PageTextData& pageData,
                           const CharPosition& pos,
                           CharPosition* start,
                           CharPosition* end);

    /**
     * @brief 判断字符是否为单词分隔符
     */
    bool isWordSeparator(QChar ch) const;

    /**
     * @brief 设置选择范围（从两个位置）
     */
    void setSelectionRange(int pageIndex,
                           const CharPosition& start,
                           const CharPosition& end,
                           SelectionMode mode = SelectionMode::Character);

    /**
     * @brief 构建选择范围
     * 计算高亮矩形和选中的文本
     */
    void buildSelection();

    /**
     * @brief 提取选择范围内的文本
     */
    QString extractSelectedText(const PageTextData& pageData);

    /**
     * @brief 计算选择范围的高亮矩形
     */
    QVector<QRectF> calculateHighlightRects(const PageTextData& pageData);

    /**
     * @brief 获取字符在页面中的全局索引
     */
    int getCharGlobalIndex(const PageTextData& pageData, const CharPosition& pos) const;

    /**
     * @brief 从全局索引获取字符位置
     */
    CharPosition getCharPositionFromIndex(const PageTextData& pageData, int index) const;

private:
    PerThreadMuPDFRenderer* m_renderer;
    TextCacheManager* m_textCache;

    TextSelection m_selection;              ///< 当前选择
    bool m_isSelecting;                     ///< 是否正在选择

    // 选择锚点（用于Shift+点击扩展选择）
    CharPosition m_anchorPos;               ///< 锚点位置
    bool m_hasAnchor;                       ///< 是否有锚点

    // 选择起点（用于拖拽）
    int m_startPageIndex;
    CharPosition m_startCharPos;
    double m_startZoom;

    // Word/Line模式的初始位置
    CharPosition m_wordStart;
    CharPosition m_wordEnd;
};

#endif // TEXTSELECTOR_H
