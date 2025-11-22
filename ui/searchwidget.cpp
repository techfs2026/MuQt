// searchwidget.cpp - 修改为使用PDFInteractionHandler

#include "searchwidget.h"
#include "pdfinteractionhandler.h"  // 【修改】引入交互处理器
#include "searchmanager.h"
#include "pdfpagewidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QStyle>

// 【修改】构造函数参数改为PDFInteractionHandler
SearchWidget::SearchWidget(PDFInteractionHandler* interactionHandler,
                           PDFPageWidget* pageWidget,
                           QWidget* parent)
    : QWidget(parent)
    , m_interactionHandler(interactionHandler)  // 【修改】
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

    // 【修改】连接交互处理器信号
    if (m_interactionHandler) {
        connect(m_interactionHandler, &PDFInteractionHandler::searchCompleted,
                this, &SearchWidget::onSearchCompleted);
        connect(m_interactionHandler, &PDFInteractionHandler::searchProgress,
                this, &SearchWidget::onSearchProgress);
        connect(m_interactionHandler, &PDFInteractionHandler::searchCancelled,
                this, [this]() {
                    m_isSearching = false;
                    updateUI();
                });
        connect(m_interactionHandler, &PDFInteractionHandler::searchError,
                this, [this](const QString& error) {
                    m_isSearching = false;
                    m_matchLabel->setText(tr("Error: %1").arg(error));
                    updateUI();
                });
    }
}

void SearchWidget::performSearch()
{
    QString query = m_searchCombo->currentText().trimmed();

    if (query.isEmpty()) {
        // 【修改】使用交互处理器
        if (m_interactionHandler) {
            m_interactionHandler->clearSearchResults();
        }
        updateUI();
        return;
    }

    // 取消之前的搜索
    if (m_isSearching && m_interactionHandler) {
        m_interactionHandler->cancelSearch();
    }

    // 【修改】使用交互处理器启动搜索
    if (m_interactionHandler) {
        bool caseSensitive = m_caseSensitiveCheck->isChecked();
        bool wholeWords = m_wholeWordsCheck->isChecked();
        int startPage = m_pageWidget->currentPage();

        m_interactionHandler->startSearch(query, caseSensitive, wholeWords, startPage);
        m_interactionHandler->addSearchHistory(query);
    }

    // 开始搜索
    m_isSearching = true;

    // 更新UI
    m_matchLabel->setText(tr("Searching..."));
    updateUI();
}

void SearchWidget::findNext()
{
    if (!m_interactionHandler || m_interactionHandler->totalSearchMatches() == 0) {
        return;
    }

    SearchResult result = m_interactionHandler->findNext();
    if (result.isValid()) {
        navigateToResult(result);
        updateUI();
    }
}

void SearchWidget::findPrevious()
{
    if (!m_interactionHandler || m_interactionHandler->totalSearchMatches() == 0) {
        return;
    }

    SearchResult result = m_interactionHandler->findPrevious();
    if (result.isValid()) {
        navigateToResult(result);
        updateUI();
    }
}

void SearchWidget::updateUI()
{
    // 【修改】从交互处理器获取信息
    int totalMatches = m_interactionHandler ?
                           m_interactionHandler->totalSearchMatches() : 0;
    int currentIndex = m_interactionHandler ?
                           m_interactionHandler->currentSearchMatchIndex() : -1;

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

    // 更新搜索历史下拉框
    if (m_interactionHandler) {
        QStringList history = m_interactionHandler->getSearchHistory(10);
        QString currentText = m_searchCombo->currentText();

        m_searchCombo->blockSignals(true);
        m_searchCombo->clear();
        m_searchCombo->addItems(history);
        m_searchCombo->setEditText(currentText);
        m_searchCombo->blockSignals(false);
    }
}

void SearchWidget::onSearchCompleted(const QString& query, int totalMatches)
{
    m_isSearching = false;
    updateUI();

    // 如果有结果，自动跳转到第一个
    if (totalMatches > 0 && m_interactionHandler) {
        // 设置当前匹配索引为0
        SearchResult result = m_interactionHandler->findNext(); // 这会设置为第0个
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
