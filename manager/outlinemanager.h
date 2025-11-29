#ifndef OUTLINEMANAGER_H
#define OUTLINEMANAGER_H

#include "outlineitem.h"
#include <QObject>

class ThreadSafeRenderer;

/**
 * @brief PDF大纲管理器
 *
 * 负责从PDF文档中提取大纲信息，构建树形结构
 * 支持大纲遍历和页面跳转
 */
class OutlineManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param renderer MuPDF渲染器指针
     * @param parent 父对象
     */
    explicit OutlineManager(ThreadSafeRenderer* renderer, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~OutlineManager();

    /**
     * @brief 加载文档大纲
     * @return 如果成功加载大纲返回true，无大纲或失败返回false
     *
     * 从当前打开的PDF文档中提取大纲信息
     * 必须在文档加载后调用
     */
    bool loadOutline();

    /**
     * @brief 清空大纲数据
     */
    void clear();

    /**
     * @brief 判断是否有大纲
     * @return 如果文档包含大纲返回true
     */
    bool hasOutline() const { return m_root && m_root->childCount() > 0; }

    /**
     * @brief 获取大纲根节点
     * @return 大纲树的根节点，如果无大纲返回nullptr
     *
     * 根节点本身不包含数据，只作为容器持有顶层大纲项
     */
    OutlineItem* root() const { return m_root; }

    /**
     * @brief 获取大纲项总数
     * @return 所有大纲项的数量（不包括根节点）
     */
    int totalItemCount() const { return m_totalItems; }

signals:
    /**
     * @brief 大纲加载完成信号
     * @param success 是否成功加载
     * @param itemCount 大纲项数量
     */
    void outlineLoaded(bool success, int itemCount);

private:
    /**
     * @brief 递归构建大纲树
     * @param fzOutline MuPDF大纲节点指针
     * @param parent 父大纲项
     * @return 构建的大纲项数量
     */
    int buildOutlineTree(void* fzOutline, OutlineItem* parent);

    /**
     * @brief 解析大纲目标页码
     * @param fzOutline MuPDF大纲节点指针
     * @return 目标页码（0-based），失败返回-1
     */
    int resolvePageIndex(void* fzOutline);

    /**
     * @brief 递归统计大纲项数量
     * @param item 大纲项
     * @return 该节点及其所有子节点的总数
     */
    int countItems(OutlineItem* item) const;

private:
    ThreadSafeRenderer* m_renderer;    ///< MuPDF渲染器
    OutlineItem* m_root;          ///< 大纲树根节点（拥有所有权）
    int m_totalItems;             ///< 大纲项总数
};

#endif // OUTLINEMANAGER_H
