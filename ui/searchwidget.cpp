#include "searchwidget.h"
#include "pdfinteractionhandler.h"
#include "pdfpagewidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QStyle>

// 【修改】构造函数参数改为PDFInteractionHandler
SearchWidget::SearchWidget(PDFDocumentSession* session,
                           PDFPageWidget* pageWidget,
                           QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_pageWidget(pageWidget)
    , m_isSearching(false)
{
    setupUI();
    setupConnections();
    updateUI();
}

void SearchWidget::showAndFocus()
{
    show();
    m_searchCombo->setFocus();
    m_searchCombo->lineEdit()->selectAll();
}

QString SearchWidget::searchText() const
{
    return m_searchCombo->currentText();
}

void SearchWidget::setupUI()
{
    // UI设置保持不变
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    QLabel* searchLabel = new QLabel(tr("Find:"), this);
    mainLayout->addWidget(searchLabel);

    m_searchCombo = new QComboBox(this);
    m_searchCombo->setEditable(true);
    m_searchCombo->setMinimumWidth(200);
    m_searchCombo->setMaxCount(20);
    m_searchCombo->setInsertPolicy(QComboBox::InsertAtTop);
    m_searchCombo->setDuplicatesEnabled(false);
    mainLayout->addWidget(m_searchCombo);

    m_previousButton = new QPushButton(tr("Previous"), this);
    m_previousButton->setEnabled(false);
    mainLayout->addWidget(m_previousButton);

    m_nextButton = new QPushButton(tr("Next"), this);
    m_nextButton->setEnabled(false);
    mainLayout->addWidget(m_nextButton);

    m_matchLabel = new QLabel(tr("No matches"), this);
    m_matchLabel->setMinimumWidth(100);
    mainLayout->addWidget(m_matchLabel);

    mainLayout->addSpacing(10);

    m_caseSensitiveCheck = new QCheckBox(tr("Case sensitive"), this);
    mainLayout->addWidget(m_caseSensitiveCheck);

    m_wholeWordsCheck = new QCheckBox(tr("Whole words"), this);
    mainLayout->addWidget(m_wholeWordsCheck);

    mainLayout->addStretch();

    m_closeButton = new QToolButton(this);
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setToolTip(tr("Close search bar (Esc)"));
    mainLayout->addWidget(m_closeButton);

    setStyleSheet(R"(
        SearchWidget {
            background-color: palette(window);
            border-bottom: 1px solid palette(dark);
        }
    )");
}

void SearchWidget::setupConnections()
{
    // 搜索输入
    connect(m_searchCombo->lineEdit(), &QLineEdit::returnPressed,
            this, &SearchWidget::performSearch);

    // 导航按钮
    connect(m_previousButton, &QPushButton::clicked, this, &SearchWidget::findPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &SearchWidget::findNext);

    // 选项变化时重新搜索
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &SearchWidget::performSearch);
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &SearchWidget::performSearch);

    // 关闭按钮
    connect(m_closeButton, &QToolButton::clicked, this, &SearchWidget::closeRequested);

    connect(m_session, &PDFDocumentSession::searchCompleted,
            this, &SearchWidget::onSearchCompleted);
    connect(m_session, &PDFDocumentSession::searchProgressUpdated,
            this, &SearchWidget::onSearchProgress);
    connect(m_session, &PDFDocumentSession::searchCancelled,
            this, [this]() {
                m_isSearching = false;
                updateUI();
            });
}

void SearchWidget::performSearch()
{
    QString query = m_searchCombo->currentText().trimmed();

    if (query.isEmpty()) {
        // 【修改】使用交互处理器
        // m_session->clearSearchResults();
        updateUI();
        return;
    }

    // 取消之前的搜索
    if (m_isSearching) {
        m_session->cancelSearch();
    }

    // 【修改】使用交互处理器启动搜索
    if (m_session) {
        bool caseSensitive = m_caseSensitiveCheck->isChecked();
        bool wholeWords = m_wholeWordsCheck->isChecked();
        int startPage = m_pageWidget->currentPage();

        m_session->startSearch(query, caseSensitive, wholeWords, startPage);
        // m_session->addSearchHistory(query);
    }

    // 开始搜索
    m_isSearching = true;

    // 更新UI
    m_matchLabel->setText(tr("Searching..."));
    updateUI();
}

void SearchWidget::findNext()
{
    // if (m_session->totalSearchMatches() == 0) {
    //     return;
    // }

    SearchResult result = m_session->findNext();
    if (result.isValid()) {
        navigateToResult(result);
        updateUI();
    }
}

void SearchWidget::findPrevious()
{
    // if (m_session->totalSearchMatches() == 0) {
    //     return;
    // }

    SearchResult result = m_session->findPrevious();
    if (result.isValid()) {
        navigateToResult(result);
        updateUI();
    }
}

void SearchWidget::updateUI()
{
    // 【修改】从交互处理器获取信息
    int totalMatches = 0; //m_session->totalSearchMatches();
    int currentIndex = 0; //m_session->currentSearchMatchIndex();

    // 更新按钮状态
    bool hasResults = totalMatches > 0;
    m_previousButton->setEnabled(hasResults && !m_isSearching);
    m_nextButton->setEnabled(hasResults && !m_isSearching);

    // 更新匹配计数
    if (m_isSearching) {
        m_matchLabel->setText(tr("Searching..."));
    } else if (totalMatches == 0) {
        QString query = m_searchCombo->currentText();
        if (query.isEmpty()) {
            m_matchLabel->setText(tr("No matches"));
        } else {
            m_matchLabel->setText(tr("No matches found"));
        }
    } else {
        m_matchLabel->setText(tr("%1 of %2")
                                  .arg(currentIndex + 1)
                                  .arg(totalMatches));
    }

    // TODO: 更新搜索历史下拉框

}

void SearchWidget::onSearchCompleted(const QString& query, int totalMatches)
{
    m_isSearching = false;
    updateUI();

    // 如果有结果，自动跳转到第一个
    if (totalMatches > 0) {
        // 设置当前匹配索引为0
        SearchResult result = m_session->findNext(); // 这会设置为第0个
        if (result.isValid()) {
            navigateToResult(result);
        }
    }
}

void SearchWidget::onSearchProgress(int currentPage, int totalPages, int matchCount)
{
    m_matchLabel->setText(tr("Searching... %1/%2 pages, %3 matches")
                              .arg(currentPage)
                              .arg(totalPages)
                              .arg(matchCount));
}

void SearchWidget::navigateToResult(const SearchResult& result)
{
    if (!result.isValid()) {
        return;
    }

    // 跳转到结果所在页面
    if (m_pageWidget->currentPage() != result.pageIndex) {
        m_pageWidget->setCurrentPage(result.pageIndex);
    }

    // 触发高亮更新
    m_pageWidget->update();
}

void SearchWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}
