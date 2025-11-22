#ifndef OUTLINEDIALOG_H
#define OUTLINEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QDialogButtonBox>

/**
 * @brief 大纲编辑对话框
 *
 * 用于添加或编辑大纲项信息
 * 包含标题输入和页码选择
 */
class OutlineDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 对话框模式
     */
    enum Mode {
        AddMode,      ///< 添加新大纲项
        EditMode      ///< 编辑现有大纲项
    };

    /**
     * @brief 构造函数
     * @param mode 对话框模式
     * @param maxPage 最大页码（用于页码范围限制）
     * @param parent 父组件
     */
    explicit OutlineDialog(Mode mode, int maxPage, QWidget* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~OutlineDialog();

    /**
     * @brief 设置大纲标题
     * @param title 标题文本
     */
    void setTitle(const QString& title);

    /**
     * @brief 获取大纲标题
     * @return 标题文本
     */
    QString title() const;

    /**
     * @brief 设置目标页码
     * @param pageIndex 页码（0-based）
     */
    void setPageIndex(int pageIndex);

    /**
     * @brief 获取目标页码
     * @return 页码（0-based）
     */
    int pageIndex() const;

    /**
     * @brief 验证输入
     * @return 输入有效返回true
     */
    bool validate();

private:
    /**
     * @brief 初始化UI
     */
    void setupUI();

    /**
     * @brief 应用样式表
     */
    void applyStyleSheet();

private slots:
    /**
     * @brief 处理确认按钮点击
     */
    void onAccepted();

private:
    Mode m_mode;                    ///< 对话框模式
    int m_maxPage;                  ///< 最大页码

    QLineEdit* m_titleEdit;         ///< 标题输入框
    QSpinBox* m_pageSpinBox;        ///< 页码选择器
    QDialogButtonBox* m_buttonBox;  ///< 按钮栏
};

#endif // OUTLINEDIALOG_H
