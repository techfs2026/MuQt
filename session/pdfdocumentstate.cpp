#include "pdfdocumentstate.h"
#include "appconfig.h"

PDFDocumentState::PDFDocumentState(QObject* parent)
    : QObject(parent)
    , m_isDocumentLoaded(false)
    , m_pageCount(0)
    , m_isTextPDF(false)
    , m_currentPage(0)
    , m_currentZoom(AppConfig::DEFAULT_ZOOM)
    , m_currentZoomMode(ZoomMode::FitWidth)
    , m_currentDisplayMode(PageDisplayMode::SinglePage)
    , m_isContinuousScroll(false)
    , m_currentRotation(0)
    , m_linksVisible(true)
    , m_hasTextSelection(false)
    , m_isSearching(false)
    , m_searchTotalMatches(0)
    , m_searchCurrentMatchIndex(-1)
{
}

PDFDocumentState::~PDFDocumentState()
{
}

// ==================== 状态更新方法 ====================

void PDFDocumentState::setDocumentLoaded(bool loaded, const QString& path,
                                         int pageCount, bool isTextPDF)
{
    bool changed = (m_isDocumentLoaded != loaded);

    m_isDocumentLoaded = loaded;
    m_documentPath = path;
    m_pageCount = pageCount;
    m_isTextPDF = isTextPDF;

    if (changed) {
        emit documentLoadedChanged(loaded, path, pageCount);

        if (loaded) {
            emit documentTypeChanged(isTextPDF);
        }
    }
}

void PDFDocumentState::setCurrentPage(int pageIndex)
{
    if (m_currentPage != pageIndex) {
        m_currentPage = pageIndex;
        emit currentPageChanged(pageIndex);
    }
}

void PDFDocumentState::setCurrentZoom(double zoom)
{
    if (qAbs(m_currentZoom - zoom) > 0.001) {
        m_currentZoom = zoom;
        emit currentZoomChanged(zoom);
    }
}

void PDFDocumentState::setCurrentZoomMode(ZoomMode mode)
{
    if (m_currentZoomMode != mode) {
        m_currentZoomMode = mode;
        emit currentZoomModeChanged(mode);
    }
}

void PDFDocumentState::setCurrentDisplayMode(PageDisplayMode mode)
{
    if (m_currentDisplayMode != mode) {
        m_currentDisplayMode = mode;
        emit currentDisplayModeChanged(mode);
    }
}

void PDFDocumentState::setContinuousScroll(bool continuous)
{
    if (m_isContinuousScroll != continuous) {
        m_isContinuousScroll = continuous;
        emit continuousScrollChanged(continuous);
    }
}

void PDFDocumentState::setCurrentRotation(int rotation)
{
    if (m_currentRotation != rotation) {
        m_currentRotation = rotation;
        emit currentRotationChanged(rotation);
    }
}

void PDFDocumentState::setPagePositions(const QVector<int>& positions,
                                        const QVector<int>& heights)
{
    m_pageYPositions = positions;
    m_pageHeights = heights;
    emit pagePositionsChanged(positions, heights);
}

void PDFDocumentState::setLinksVisible(bool visible)
{
    if (m_linksVisible != visible) {
        m_linksVisible = visible;
        emit linksVisibleChanged(visible);
    }
}

void PDFDocumentState::setHasTextSelection(bool has)
{
    if (m_hasTextSelection != has) {
        m_hasTextSelection = has;
        emit textSelectionChanged(has);
    }
}

void PDFDocumentState::setSearchState(bool searching, int totalMatches, int currentIndex)
{
    bool changed = (m_isSearching != searching) ||
                   (m_searchTotalMatches != totalMatches) ||
                   (m_searchCurrentMatchIndex != currentIndex);

    if (changed) {
        m_isSearching = searching;
        m_searchTotalMatches = totalMatches;
        m_searchCurrentMatchIndex = currentIndex;
        emit searchStateChanged(searching, totalMatches, currentIndex);
    }
}

void PDFDocumentState::reset()
{
    setDocumentLoaded(false, QString(), 0, false);
    setCurrentPage(0);
    setCurrentZoom(AppConfig::DEFAULT_ZOOM);
    setCurrentZoomMode(ZoomMode::FitWidth);
    setCurrentDisplayMode(PageDisplayMode::SinglePage);
    setContinuousScroll(false);
    setCurrentRotation(0);
    setPagePositions(QVector<int>(), QVector<int>());
    setLinksVisible(true);
    setHasTextSelection(false);
    setSearchState(false, 0, -1);
}
