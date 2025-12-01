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
 *
 * 设计原则：
 * - 只存最终状态，不存计算中间值
 * - 状态变更由Session统一管理
 */
class PDFDocumentState : public QObject
{
    Q_OBJECT

public:
    explicit PDFDocumentState(QObject* parent = nullptr);
    ~PDFDocumentState();

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

    /**
     * @brief 获取当前页码（0-based）
     */
    int currentPage() const { return m_currentPage; }


    /**
     * @brief 获取当前缩放比例
     */
    double currentZoom() const { return m_currentZoom; }

    /**
     * @brief 获取缩放模式
     */
    ZoomMode currentZoomMode() const { return m_currentZoomMode; }


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


    /**
     * @brief 获取页面Y位置列表
     */
    const QVector<int>& pageYPositions() const { return m_pageYPositions; }

    /**
     * @brief 获取页面高度列表
     */
    const QVector<int>& pageHeights() const { return m_pageHeights; }

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

    // 保存当前视口状态（相对位置）
    void saveViewportState(int scrollY);

    // 获取需要恢复的滚动位置（基于新的positions计算）
    int getRestoredScrollPosition(int margin) const;

    // 是否需要恢复视口
    bool needRestoreViewport() const { return m_viewportRestore.needRestore; }

    // 清除恢复状态
    void clearViewportRestore() { m_viewportRestore.reset(); }


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
    ViewportRestoreState m_viewportRestore;

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
