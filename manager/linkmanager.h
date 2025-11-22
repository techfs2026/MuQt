#ifndef LINKMANAGER_H
#define LINKMANAGER_H

#include <QObject>
#include <QRectF>
#include <QVector>
#include <QString>
#include <QMap>

class MuPDFRenderer;

/**
 * @brief PDF链接信息
 */
struct PDFLink
{
    QRectF rect;          ///< 链接区域（页面坐标）
    int targetPage;       ///< 目标页码（-1表示外部链接）
    QString uri;          ///< 链接URI

    /**
     * @brief 判断是否为内部链接
     */
    bool isInternal() const { return targetPage >= 0; }

    /**
     * @brief 判断是否为外部链接
     */
    bool isExternal() const { return !uri.isEmpty() && targetPage < 0; }
};

/**
 * @brief PDF链接管理器
 *
 * 负责提取和管理PDF页面中的链接
 * 支持内部链接（页面跳转）和外部链接（URL）
 */
class LinkManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param renderer MuPDF渲染器指针
     * @param parent 父对象
     */
    explicit LinkManager(MuPDFRenderer* renderer, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~LinkManager();

    /**
     * @brief 加载指定页面的链接
     * @param pageIndex 页码（0-based）
     * @return 链接列表
     */
    QVector<PDFLink> loadPageLinks(int pageIndex);

    /**
     * @brief 检测点击位置是否在链接区域
     * @param pageIndex 页码（0-based）
     * @param pos 点击位置（页面坐标）
     * @param zoom 缩放比例
     * @return 找到的链接指针，未找到返回nullptr
     */
    const PDFLink* hitTestLink(int pageIndex, const QPointF& pos, double zoom);

    /**
     * @brief 清空缓存的链接
     */
    void clear();

signals:
    /**
     * @brief 请求跳转到指定页面
     * @param pageIndex 目标页码（0-based）
     */
    void pageJumpRequested(int pageIndex);

    /**
     * @brief 请求打开外部链接
     * @param uri 链接URI
     */
    void externalLinkRequested(const QString& uri);

private:
    /**
     * @brief 解析MuPDF链接目标
     * @param fzLink MuPDF链接指针
     * @return 目标页码，失败返回-1
     */
    int resolveLinkTarget(void* fzLink);

private:
    MuPDFRenderer* m_renderer;                       ///< MuPDF渲染器
    QMap<int, QVector<PDFLink>> m_cachedLinks;      ///< 缓存的链接（按页索引）
};

#endif // LINKMANAGER_H
