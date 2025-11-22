#ifndef OUTLINEITEM_H
#define OUTLINEITEM_H

#include <QString>
#include <QList>
#include <QMetaType>


/**
 * @brief PDF大纲项数据结构
 *
 * 表示PDF文档中的一个大纲节点，包含标题、目标页码等信息
 * 支持树形结构，每个节点可以有多个子节点
 */
class OutlineItem
{
public:
    /**
     * @brief 构造函数
     * @param title 大纲标题
     * @param pageIndex 目标页码（0-based）
     * @param uri 外部链接URI（如果有）
     */
    OutlineItem(const QString& title = QString(),
                int pageIndex = -1,
                const QString& uri = QString());

    /**
     * @brief 析构函数
     */
    ~OutlineItem();

    // 禁用拷贝
    OutlineItem(const OutlineItem&) = delete;
    OutlineItem& operator=(const OutlineItem&) = delete;

    // ========== 属性访问 ==========

    /** @brief 获取大纲标题 */
    QString title() const { return m_title; }

    /** @brief 设置大纲标题 */
    void setTitle(const QString& title) { m_title = title; }

    /** @brief 获取目标页码（0-based，-1表示无效） */
    int pageIndex() const { return m_pageIndex; }

    /** @brief 设置目标页码 */
    void setPageIndex(int index) { m_pageIndex = index; }

    /** @brief 获取外部链接URI */
    QString uri() const { return m_uri; }

    /** @brief 设置外部链接URI */
    void setUri(const QString& uri) { m_uri = uri; }

    /** @brief 判断是否为外部链接 */
    bool isExternalLink() const { return !m_uri.isEmpty(); }

    /** @brief 判断是否有效（有标题或有目标） */
    bool isValid() const { return !m_title.isEmpty() || m_pageIndex >= 0; }

    // ========== 树形结构管理 ==========

    /** @brief 获取父节点 */
    OutlineItem* parent() const { return m_parent; }

    /** @brief 设置父节点（仅内部使用） */
    void setParent(OutlineItem* parent) { m_parent = parent; }

    /** @brief 获取子节点列表 */
    const QList<OutlineItem*>& children() const { return m_children; }

    /**
     * @brief 添加子节点
     * @param child 子节点指针（OutlineItem获得所有权）
     */
    void addChild(OutlineItem* child);

    /**
     * @brief 在指定位置插入子节点
     * @param index 插入位置
     * @param child 子节点指针
     * @return 成功返回true
     */
    bool insertChild(int index, OutlineItem* child);

    /**
     * @brief 移除子节点（不删除对象）
     * @param child 要移除的子节点
     * @return 成功返回true
     */
    bool removeChild(OutlineItem* child);

    /**
     * @brief 移除指定索引的子节点（不删除对象）
     * @param index 子节点索引
     * @return 移除的节点指针，失败返回nullptr
     */
    OutlineItem* takeChild(int index);

    /**
     * @brief 清空所有子节点（删除所有子对象）
     */
    void clearChildren();

    /** @brief 获取子节点数量 */
    int childCount() const { return m_children.size(); }

    /** @brief 获取指定索引的子节点 */
    OutlineItem* child(int index) const;

    /**
     * @brief 获取子节点的索引
     * @param child 子节点指针
     * @return 索引值，未找到返回-1
     */
    int indexOf(OutlineItem* child) const;

    /** @brief 获取节点深度（根节点为0） */
    int depth() const;

private:
    QString m_title;              ///< 大纲标题
    int m_pageIndex;              ///< 目标页码（0-based）
    QString m_uri;                ///< 外部链接URI

    OutlineItem* m_parent;        ///< 父节点指针（不拥有所有权）
    QList<OutlineItem*> m_children;  ///< 子节点列表（拥有所有权，在析构时释放）
};

// 声明为Qt元类型，用于QVariant存储
Q_DECLARE_METATYPE(OutlineItem*)

#endif // OUTLINEITEM_H
