#ifndef OUTLINEEDITOR_H
#define OUTLINEEDITOR_H

#include <QObject>
#include <QString>

class MuPDFRenderer;
class OutlineItem;

/**
 * @brief PDF大纲编辑器
 *
 * 负责PDF文档大纲的增删改操作和持久化保存
 * 通过操作PDF的Outline对象树实现大纲编辑功能
 */
class OutlineEditor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param renderer MuPDF渲染器指针
     * @param parent 父对象
     */
    explicit OutlineEditor(MuPDFRenderer* renderer, QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~OutlineEditor();

    /**
     * @brief 添加大纲项
     * @param parentItem 父大纲节点（nullptr表示添加到根层级）
     * @param title 大纲标题
     * @param pageIndex 目标页码（0-based）
     * @param insertIndex 插入位置（-1表示添加到末尾）
     * @return 成功返回新创建的大纲节点，失败返回nullptr
     *
     * 在指定位置创建新大纲节点
     */
    OutlineItem* addOutline(OutlineItem* parentItem,
                            const QString& title,
                            int pageIndex,
                            int insertIndex = -1);

    /**
     * @brief 删除大纲项
     * @param item 要删除的大纲节点
     * @return 成功返回true
     *
     * 删除指定大纲项及其所有子项
     */
    bool deleteOutline(OutlineItem* item);

    /**
     * @brief 重命名大纲项
     * @param item 要重命名的大纲节点
     * @param newTitle 新标题
     * @return 成功返回true
     *
     * 修改大纲项的标题
     */
    bool renameOutline(OutlineItem* item, const QString& newTitle);

    /**
     * @brief 移动大纲项
     * @param item 要移动的大纲节点
     * @param newParent 新的父节点（nullptr表示移动到根层级）
     * @param newIndex 新位置索引（-1表示添加到末尾）
     * @return 成功返回true
     *
     * 移动大纲项到新的位置（可以改变层级或顺序）
     */
    bool moveOutline(OutlineItem* item, OutlineItem* newParent, int newIndex = -1);

    /**
     * @brief 保存修改到PDF文件
     * @param filePath 保存路径（空字符串表示覆盖原文件）
     * @return 成功返回true
     *
     * 将书签修改写入PDF文档
     * 建议在保存前创建备份
     */
    bool saveToDocument(const QString& filePath = QString());

    /**
     * @brief 判断是否有未保存的修改
     * @return 如果有修改返回true
     */
    bool hasUnsavedChanges() const { return m_modified; }

    /**
     * @brief 重置修改标志
     *
     * 在成功保存后调用
     */
    void resetModifiedFlag() { m_modified = false; }

    /**
     * @brief 获取大纲根节点
     * @return 大纲树的根节点
     *
     * 用于访问修改后的书签树结构
     */
    OutlineItem* root() const { return m_root; }

    /**
     * @brief 设置大纲根节点
     * @param root 新的根节点
     *
     * 用于加载现有书签树
     */
    void setRoot(OutlineItem* root);

signals:
    /**
     * @brief 大纲树已修改信号
     */
    void outlineModified();

    /**
     * @brief 保存完成信号
     * @param success 是否成功
     * @param errorMsg 错误信息（成功时为空）
     */
    void saveCompleted(bool success, const QString& errorMsg);

private:
    /**
     * @brief 创建PDF大纲对象
     * @param ctx MuPDF上下文
     * @param doc PDF文档
     * @param item 大纲数据节点
     * @return 创建的pdf_obj对象（需要调用者释放）
     *
     * 将OutlineItem转换为PDF的Outline对象
     */
    void* createPdfOutline(void* ctx, void* doc, OutlineItem* item);

    /**
     * @brief 递归构建PDF大纲树
     * @param ctx MuPDF上下文
     * @param doc PDF文档
     * @param item 大纲数据节点
     * @param pdfParent 父级PDF对象
     * @return 创建的PDF大纲对象
     *
     * 递归处理所有子节点
     */
    void* buildPdfOutlineTree(void* ctx, void* doc, OutlineItem* item, void* pdfParent);

    /**
     * @brief 验证大纲参数
     * @param title 大纲标题
     * @param pageIndex 页码
     * @return 有效返回true
     */
    bool validateOutline(const QString& title, int pageIndex) const;

    /**
     * @brief 查找大纲项在父节点中的索引
     * @param item 大纲节点
     * @return 索引值，未找到返回-1
     */
    int findItemIndex(OutlineItem* item) const;

    /**
     * @brief 从父节点中移除大纲项
     * @param item 要移除的大纲节点
     * @return 成功返回true
     *
     * 只是从树结构中移除，不删除对象
     */
    bool removeFromParent(OutlineItem* item);

    /**
     * @brief 创建文件备份
     * @param filePath 原文件路径
     * @return 备份文件路径，失败返回空字符串
     */
    QString createBackup(const QString& filePath) const;

private:
    MuPDFRenderer* m_renderer;    ///< MuPDF渲染器
    OutlineItem* m_root;          ///< 大纲树根节点（不拥有所有权）
    bool m_modified;              ///< 修改标志
};

#endif // OUTLINEEDITOR_H
