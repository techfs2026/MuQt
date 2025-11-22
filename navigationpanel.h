#ifndef NAVIGATIONPANEL_H
#define NAVIGATIONPANEL_H

#include <QDockWidget>

// Forward declarations
class PDFDocumentSession;  // 新增:从Session获取所有组件
class OutlineWidget;
class ThumbnailWidget;
class QTabWidget;
class QToolButton;
class QImage;

class NavigationPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit NavigationPanel(PDFDocumentSession* session, QWidget* parent = nullptr);
    ~NavigationPanel();

    void loadDocument(int pageCount);
    void clear();
    void updateCurrentPage(int pageIndex);
    void setThumbnail(int pageIndex, const QImage& thumbnail);

signals:
    void pageJumpRequested(int pageIndex);
    void externalLinkRequested(const QString& uri);
    void outlineModified();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void setupConnections();

private:
    // Session引用(不拥有所有权)
    PDFDocumentSession* m_session;

    // UI组件
    QTabWidget* m_tabWidget;
    OutlineWidget* m_outlineWidget;
    ThumbnailWidget* m_thumbnailWidget;
    QToolButton* m_expandAllBtn;
    QToolButton* m_collapseAllBtn;
};

#endif // NAVIGATIONPANEL_H
