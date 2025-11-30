#include "pdfinteractionhandler.h"
#include "perthreadmupdfrenderer.h"
#include "textcachemanager.h"
#include "searchmanager.h"
#include "linkmanager.h"
#include "textselector.h"
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>

PDFInteractionHandler::PDFInteractionHandler(PerThreadMuPDFRenderer* renderer,
                                             TextCacheManager* textCacheManager,
                                             QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_textCacheManager(textCacheManager)
    , m_hoveredLink(nullptr)
{
    if (!m_renderer) {
        qWarning() << "PDFInteractionHandler: renderer is null!";
        return;
    }

    if (!m_textCacheManager) {
        qWarning() << "PDFInteractionHandler: textCacheManager is null!";
        return;
    }

    // 创建子管理器
    m_searchManager = std::make_unique<SearchManager>(m_renderer, m_textCacheManager, this);
    m_linkManager = std::make_unique<LinkManager>(m_renderer, this);
    m_textSelector = std::make_unique<TextSelector>(m_renderer, m_textCacheManager, this);

    // 连接信号
    setupConnections();
}

PDFInteractionHandler::~PDFInteractionHandler()
{
}

// ==================== 搜索相关 ====================

void PDFInteractionHandler::startSearch(const QString& query,
                                        bool caseSensitive,
                                        bool wholeWords,
                                        int startPage)
{
    if (!m_searchManager) {
        qWarning() << "PDFInteractionHandler: searchManager not initialized";
        return;
    }

    if (query.isEmpty()) {
        clearSearchResults();
        return;
    }

    SearchOptions options;
    options.caseSensitive = caseSensitive;
    options.wholeWords = wholeWords;
    options.maxResults = 1000;

    m_searchManager->startSearch(query, options, startPage);
}

void PDFInteractionHandler::cancelSearch()
{
    if (m_searchManager) {
        m_searchManager->cancelSearch();
    }
}

SearchResult PDFInteractionHandler::findNext()
{
    if (!m_searchManager) {
        return SearchResult();
    }

    SearchResult result = m_searchManager->nextMatch();

    if (result.isValid()) {
        int currentIndex = m_searchManager->currentMatchIndex();
        int totalMatches = m_searchManager->totalMatches();
        emit searchNavigationCompleted(result, currentIndex, totalMatches);
    }

    return result;
}

SearchResult PDFInteractionHandler::findPrevious()
{
    if (!m_searchManager) {
        return SearchResult();
    }

    SearchResult result = m_searchManager->previousMatch();

    if (result.isValid()) {
        int currentIndex = m_searchManager->currentMatchIndex();
        int totalMatches = m_searchManager->totalMatches();
        emit searchNavigationCompleted(result, currentIndex, totalMatches);
    }

    return result;
}

void PDFInteractionHandler::clearSearchResults()
{
    if (m_searchManager) {
        m_searchManager->clearResults();
        // 清除后发出搜索完成信号，匹配数为0
        emit searchCompleted(QString(), 0);
    }
}

QVector<SearchResult> PDFInteractionHandler::getPageSearchResults(int pageIndex) const
{
    if (!m_searchManager) {
        return QVector<SearchResult>();
    }
    return m_searchManager->getPageResults(pageIndex);
}

void PDFInteractionHandler::addSearchHistory(const QString& query)
{
    if (m_searchManager) {
        m_searchManager->addToHistory(query);
    }
}

QStringList PDFInteractionHandler::getSearchHistory(int maxCount) const
{
    if (!m_searchManager) {
        return QStringList();
    }
    return m_searchManager->getHistory(maxCount);
}

// ==================== 链接相关 ====================

void PDFInteractionHandler::requestSetLinksVisible(bool visible)
{
    if (!visible) {
        clearHoveredLink();
    }

    emit linksVisibilityChanged(visible);
}

const PDFLink* PDFInteractionHandler::hitTestLink(int pageIndex,
                                                  const QPointF& pagePos,
                                                  double zoom)
{
    if (!m_linkManager) {
        return nullptr;
    }

    const PDFLink* link = m_linkManager->hitTestLink(pageIndex, pagePos, zoom);

    if (link != m_hoveredLink) {
        m_hoveredLink = link;
        emit linkHovered(link);
    }

    return link;
}

void PDFInteractionHandler::clearHoveredLink()
{
    if (m_hoveredLink) {
        m_hoveredLink = nullptr;
        emit linkHovered(nullptr);
    }
}

bool PDFInteractionHandler::handleLinkClick(const PDFLink* link)
{
    if (!link) {
        return false;
    }

    emit linkClicked(link);

    // 处理内部链接
    if (link->isInternal()) {
        emit internalLinkRequested(link->targetPage);
        return true;
    }

    // 处理外部链接
    if (link->isExternal()) {
        QUrl url(link->uri);
        if (!url.isValid()) {
            emit linkError(tr("Invalid link URI: %1").arg(link->uri));
            return false;
        }

        if (!QDesktopServices::openUrl(url)) {
            emit linkError(tr("Failed to open link: %1").arg(link->uri));
            return false;
        }

        emit externalLinkRequested(link->uri);
        return true;
    }

    return false;
}

QVector<PDFLink> PDFInteractionHandler::loadPageLinks(int pageIndex)
{
    if (!m_linkManager) {
        return QVector<PDFLink>();
    }
    return m_linkManager->loadPageLinks(pageIndex);
}

// ==================== 文本选择相关 ====================

void PDFInteractionHandler::startTextSelection(int pageIndex,
                                               const QPointF& pagePos,
                                               double zoom)
{
    if (!m_textSelector) {
        qWarning() << "PDFInteractionHandler: textSelector not initialized";
        return;
    }

    m_textSelector->startSelection(pageIndex, pagePos, zoom, SelectionMode::Character);
}

void PDFInteractionHandler::updateTextSelection(int pageIndex,
                                                const QPointF& pagePos,
                                                double zoom)
{
    if (m_textSelector) {
        m_textSelector->updateSelection(pageIndex, pagePos, zoom);
    }
}

void PDFInteractionHandler::extendTextSelection(int pageIndex,
                                                const QPointF& pagePos,
                                                double zoom)
{
    if (m_textSelector) {
        m_textSelector->extendSelection(pageIndex, pagePos, zoom);
    }
}

void PDFInteractionHandler::endTextSelection()
{
    if (m_textSelector) {
        m_textSelector->endSelection();
    }
}

void PDFInteractionHandler::clearTextSelection()
{
    if (m_textSelector) {
        m_textSelector->clearSelection();
        // 清除后发出状态变化信号
        emit textSelectionChanged(false, QString());
    }
}

void PDFInteractionHandler::selectWord(int pageIndex,
                                       const QPointF& pagePos,
                                       double zoom)
{
    if (m_textSelector) {
        m_textSelector->selectWord(pageIndex, pagePos, zoom);
    }
}

void PDFInteractionHandler::selectLine(int pageIndex,
                                       const QPointF& pagePos,
                                       double zoom)
{
    if (m_textSelector) {
        m_textSelector->selectLine(pageIndex, pagePos, zoom);
    }
}

void PDFInteractionHandler::selectAll(int pageIndex)
{
    if (m_textSelector) {
        m_textSelector->selectAll(pageIndex);
    }
}

QString PDFInteractionHandler::getSelectedText() const
{
    return m_textSelector ? m_textSelector->selectedText() : QString();
}

const TextSelection& PDFInteractionHandler::getCurrentTextSelection() const
{
    static TextSelection emptySelection;
    return m_textSelector ? m_textSelector->currentSelection() : emptySelection;
}

void PDFInteractionHandler::copySelectedText()
{
    if (m_textSelector && m_textSelector->hasSelection()) {
        m_textSelector->copyToClipboard();
        emit textCopied(m_textSelector->selectedText().length());
    }
}

bool PDFInteractionHandler::isTextSelecting() const
{
    return m_textSelector && m_textSelector->isSelecting();
}

// ==================== 私有方法 ====================

void PDFInteractionHandler::setupConnections()
{
    // 连接搜索管理器信号
    if (m_searchManager) {
        connect(m_searchManager.get(), &SearchManager::searchProgress,
                this, &PDFInteractionHandler::searchProgressUpdated);
        connect(m_searchManager.get(), &SearchManager::searchCompleted,
                this, &PDFInteractionHandler::searchCompleted);
        connect(m_searchManager.get(), &SearchManager::searchCancelled,
                this, &PDFInteractionHandler::searchCancelled);
        connect(m_searchManager.get(), &SearchManager::searchError,
                this, &PDFInteractionHandler::searchError);
    }

    // 连接文本选择器信号
    if (m_textSelector) {
        connect(m_textSelector.get(), &TextSelector::selectionChanged,
                this, [this]() {
                    bool hasSelection = m_textSelector->hasSelection();
                    QString selectedText = m_textSelector->selectedText();
                    emit textSelectionChanged(hasSelection, selectedText);
                });
    }
}
