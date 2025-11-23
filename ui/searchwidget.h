#ifndef SEARCHWIDGET_H
#define SEARCHWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QToolButton>
#include "pdfdocumentsession.h"

class PDFPageWidget;

/**
 * @brief 搜索工具栏小部件
 *
 * 提供搜索界面，包括：
 * - 搜索输入框（带历史）
 * - 上一个/下一个按钮
 * - 匹配计数显示
 * - 搜索选项（大小写、整词）
 * - 关闭按钮
 */
class SearchWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param searchManager 搜索管理器
     * @param pageWidget 页面显示组件
     * @param parent 父窗口
     */
    explicit SearchWidget(PDFDocumentSession* session,
                          PDFPageWidget* pageWidget,
                          QWidget* parent);

    /**
     * @brief 显示搜索栏并聚焦输入框
     */
    void showAndFocus();

    /**
     * @brief 获取当前搜索文本
     */
    QString searchText() const;

signals:
    /**
     * @brief 关闭搜索栏信号
     */
    void closeRequested();

public slots:
    /**
     * @brief 执行搜索
     */
    void performSearch();

    /**
     * @brief 跳转到下一个匹配
     */
    void findNext();

    /**
     * @brief 跳转到上一个匹配
     */
    void findPrevious();

    /**
     * @brief 更新UI状态
     */
    void updateUI();

    /**
     * @brief 搜索完成处理
     */
    void onSearchCompleted(const QString& query, int totalMatches);

    /**
     * @brief 搜索进度处理
     */
    void onSearchProgress(int currentPage, int totalPages, int matchCount);

private:
    /**
     * @brief 初始化UI
     */
    void setupUI();

    /**
     * @brief 连接信号
     */
    void setupConnections();

    /**
     * @brief 跳转到搜索结果
     */
    void navigateToResult(const struct SearchResult& result);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    PDFDocumentSession* m_session;

    PDFPageWidget* m_pageWidget;            ///< 页面显示组件

    // UI组件
    QComboBox* m_searchCombo;               ///< 搜索输入框（带历史）
    QPushButton* m_previousButton;          ///< 上一个按钮
    QPushButton* m_nextButton;              ///< 下一个按钮
    QLabel* m_matchLabel;                   ///< 匹配计数标签
    QCheckBox* m_caseSensitiveCheck;        ///< 大小写敏感选项
    QCheckBox* m_wholeWordsCheck;           ///< 整词匹配选项
    QToolButton* m_closeButton;             ///< 关闭按钮

    bool m_isSearching;                     ///< 是否正在搜索
};

#endif // SEARCHWIDGET_H
