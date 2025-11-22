#ifndef PDFDOCUMENTSTATE_H
#define PDFDOCUMENTSTATE_H

#include <QObject>
#include <QString>
#include <QVector>
#include "datastructure.h"

/**
 * @brief PDF文档状态对象 - 集中管理文档的所有状态
 *
 * 职责：
 * 1. 存储文档的所有最终状态（不含中间状态）
 * 2. 提供状态查询接口
 * 3. 状态变更时发出信号
 *
 * 设计原则：
 * - 只存最终状态，不存计算中间值
 * - 状态变更由Session统一管理
 * - 发出的信号命名为 xxxChanged
 */
class PDFDocumentState : public QObject
{
    Q_OBJECT

public:
    explicit PDFDocumentState(QObject* parent = nullptr);
    ~PDFDocumentState();

    // ==================== 文档基本信息 ====================

    /**
     * @brief 是否已加载文档
     */
    bool isDocumentLoaded() const { return m_isDocumentLoaded; }

    /**
     * @brief 获取文档路径
     */
    QString documentPath() const { return m_documentPath; }

    /**
     * @brief 获取文档页数
     */
    int pageCount() const { return m_pageCount; }

    /**
     * @brief 是否为文本PDF
     */
    bool isTextPDF() const { return m_isTextPDF; }

    // ==================== 导航状态 ====================

    /**
     * @brief 获取当前页码（0-based）
     */
    int currentPage() const { return m_currentPage; }

    // ==================== 缩放状态 ====================

    /**
     * @brief 获取当前缩放比例
     */
    double currentZoom() const { return m_currentZoom; }

    /**
     * @brief 获取缩放模式
     */
    ZoomMode currentZoomMode() const { return m_currentZoomMode; }

    // ==================== 显示模式状态 ====================

    /**
     * @brief 获取显示模式
     */
    PageDisplayMode currentDisplayMode() const { return m_currentDisplayMode; }

    /**
     * @brief 是否为连续滚动模式
     */
    bool isContinuousScroll() const { return m_isContinuousScroll; }

    /**
     * @brief 获取旋转角度
     */
    int currentRotation() const { return m_currentRotation; }

    // ==================== 连续滚动状态 ====================

    /**
     * @brief 获取页面Y位置列表
     */
    const QVector<int>& pageYPositions() const { return m_pageYPositions; }

    /**
     * @brief 获取页面高度列表
     */
    const QVector<int>& pageHeights() const { return m_pageHeights; }

    // ==================== 交互状态 ====================

    /**
     * @brief 链接是否可见
     */
    bool linksVisible() const { return m_linksVisible; }

    /**
     * @brief 是否有文本选择
     */
    bool hasTextSelection() const { return m_hasTextSelection; }

    /**
     * @brief 是否正在搜索
     */
    bool isSearching() const { return m_isSearching; }

    /**
     * @brief 搜索总匹配数
     */
    int searchTotalMatches() const { return m_searchTotalMatches; }

    /**
     * @brief 当前搜索匹配索引
     */
    int searchCurrentMatchIndex() const { return m_searchCurrentMatchIndex; }

    // ==================== 状态更新方法（由Session调用）====================

    void setDocumentLoaded(bool loaded, const QString& path = QString(),
                           int pageCount = 0, bool isTextPDF = false);
    void setCurrentPage(int pageIndex);
    void setCurrentZoom(double zoom);
    void setCurrentZoomMode(ZoomMode mode);
    void setCurrentDisplayMode(PageDisplayMode mode);
    void setContinuousScroll(bool continuous);
    void setCurrentRotation(int rotation);
    void setPagePositions(const QVector<int>& positions, const QVector<int>& heights);
    void setLinksVisible(bool visible);
    void setHasTextSelection(bool has);
    void setSearchState(bool searching, int totalMatches = 0, int currentIndex = -1);

    /**
     * @brief 重置所有状态
     */
    void reset();

signals:
    // ==================== 文档状态变化信号 ====================

    /**
     * @brief 文档加载状态变化
     */
    void documentLoadedChanged(bool loaded, const QString& path, int pageCount);

    /**
     * @brief 文档类型检测完成
     */
    void documentTypeChanged(bool isTextPDF);

    // ==================== 导航状态变化信号 ====================

    /**
     * @brief 当前页码变化
     */
    void currentPageChanged(int pageIndex);

    // ==================== 缩放状态变化信号 ====================

    /**
     * @brief 当前缩放比例变化
     */
    void currentZoomChanged(double zoom);

    /**
     * @brief 缩放模式变化
     */
    void currentZoomModeChanged(ZoomMode mode);

    // ==================== 显示模式状态变化信号 ====================

    /**
     * @brief 显示模式变化
     */
    void currentDisplayModeChanged(PageDisplayMode mode);

    /**
     * @brief 连续滚动模式变化
     */
    void continuousScrollChanged(bool continuous);

    /**
     * @brief 旋转角度变化
     */
    void currentRotationChanged(int rotation);

    // ==================== 连续滚动状态变化信号 ====================

    /**
     * @brief 页面位置计算完成
     */
    void pagePositionsChanged(const QVector<int>& positions, const QVector<int>& heights);

    // ==================== 交互状态变化信号 ====================

    /**
     * @brief 链接可见性变化
     */
    void linksVisibleChanged(bool visible);

    /**
     * @brief 文本选择状态变化
     */
    void textSelectionChanged(bool hasSelection);

    /**
     * @brief 搜索状态变化
     */
    void searchStateChanged(bool searching, int totalMatches, int currentIndex);

private:
    // 文档基本信息
    bool m_isDocumentLoaded;
    QString m_documentPath;
    int m_pageCount;
    bool m_isTextPDF;

    // 导航状态
    int m_currentPage;

    // 缩放状态
    double m_currentZoom;
    ZoomMode m_currentZoomMode;

    // 显示模式状态
    PageDisplayMode m_currentDisplayMode;
    bool m_isContinuousScroll;
    int m_currentRotation;

    // 连续滚动状态
    QVector<int> m_pageYPositions;
    QVector<int> m_pageHeights;

    // 交互状态
    bool m_linksVisible;
    bool m_hasTextSelection;
    bool m_isSearching;
    int m_searchTotalMatches;
    int m_searchCurrentMatchIndex;
};

#endif // PDFDOCUMENTSTATE_H
