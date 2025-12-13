#ifndef NAVIGATIONPANEL_H
#define NAVIGATIONPANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QLabel>
#include <QProgressBar>

class PDFDocumentSession;
class OutlineWidget;
class ThumbnailWidget;
class QToolButton;

class NavigationPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NavigationPanel(PDFDocumentSession* session, QWidget* parent = nullptr);
    ~NavigationPanel();

    void loadDocument(int pageCount);
    void clear();
    void updateCurrentPage(int pageIndex);

signals:
    void pageJumpRequested(int pageIndex);
    void externalLinkRequested(const QString& uri);
    void outlineModified();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTabChanged(int index);

private:
    void setupUI();
    void setupConnections();

    PDFDocumentSession* m_session;
    QTabWidget* m_tabWidget;
    OutlineWidget* m_outlineWidget;
    ThumbnailWidget* m_thumbnailWidget;
    QToolButton* m_expandAllBtn;
    QToolButton* m_collapseAllBtn;

    // 缩略图状态提示
    QLabel* m_thumbnailStatusLabel;
    QProgressBar* m_thumbnailProgressBar;
};

#endif // NAVIGATIONPANEL_H
