#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QSettings>
#include <QString>
#include <QColor>
#include <QSize>

/**
 * @brief 应用配置管理类
 *
 * 负责管理应用的所有配置项，包括：
 * - 渲染相关配置
 * - 缓存策略配置
 * - UI布局配置
 * - 用户偏好设置
 *
 * 使用单例模式，通过 QSettings 持久化配置
 */
class AppConfig
{
public:
    /**
     * @brief 获取单例实例
     */
    static AppConfig& instance();

    // ========== 渲染配置 ==========

    /// 最小缩放比例
    static constexpr double MIN_ZOOM = 0.25;

    /// 最大缩放比例
    static constexpr double MAX_ZOOM = 5.0;

    /// 缩放步进
    static constexpr double ZOOM_STEP = 0.25;

    /// 默认缩放比例
    static constexpr double DEFAULT_ZOOM = 1.0;

    /// 默认DPI
    static constexpr int DEFAULT_DPI = 72;

    // ========== 布局配置 ==========

    /// 页面边距
    static constexpr int PAGE_MARGIN = 20;

    /// 页面间隔（连续滚动模式）
    static constexpr int PAGE_GAP = 10;

    /// 阴影偏移
    static constexpr int SHADOW_OFFSET = 3;

    /// 双页模式页面间距
    static constexpr int DOUBLE_PAGE_SPACING = 10;

    // ========== 文本缓存配置 ==========

    /**
     * @brief 文本缓存最大页数
     * -1 表示不限制(缓存所有页面)
     */
    static constexpr int MAX_TEXT_CACHE_SIZE = -1;

    /**
     * @brief PDF类型检测时的采样页数
     */
    static constexpr int PDF_TYPE_DETECT_SAMPLE_PAGES = 5;

    /**
     * @brief 文本型PDF判断阈值
     * 如果采样页中包含文本的比例超过此值,认为是文本型PDF
     */
    static constexpr double TEXT_PDF_THRESHOLD = 0.3;  // 30%

    /**
     * @brief 文本预加载优先页数
     * 文档加载时优先加载前N页的文本
     */
    static constexpr int TEXT_PRELOAD_PRIORITY_PAGES = 10;

    // ========== 缓存配置 ==========

    /// 最大缓存页面数
    int maxCacheSize() const { return m_maxCacheSize; }
    void setMaxCacheSize(int size);

    /// 预加载边距（像素）
    int preloadMargin() const { return m_preloadMargin; }
    void setPreloadMargin(int margin);

    // ========== 性能配置 ==========

    /// Resize防抖延迟（毫秒）
    int resizeDebounceDelay() const { return m_resizeDebounceDelay; }
    void setResizeDebounceDelay(int delay);

    // ========== UI配置 ==========

    /// 背景颜色
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);

    /// 窗口默认大小
    QSize defaultWindowSize() const { return m_defaultWindowSize; }
    void setDefaultWindowSize(const QSize& size);

    /**
     * @brief 获取文本缓存最大页数
     */
    int maxTextCacheSize() const { return MAX_TEXT_CACHE_SIZE; }

    /**
     * @brief 获取PDF类型检测采样页数
     */
    int pdfTypeDetectSamplePages() const { return PDF_TYPE_DETECT_SAMPLE_PAGES; }

    /**
     * @brief 获取文本预加载优先页数
     */
    int textPreloadPriorityPages() const { return TEXT_PRELOAD_PRIORITY_PAGES; }

    // ========== 用户偏好 ==========

    /// 记住上次打开的文件
    bool rememberLastFile() const { return m_rememberLastFile; }
    void setRememberLastFile(bool remember);

    /// 上次打开的文件路径
    QString lastFilePath() const;
    void setLastFilePath(const QString& path);

    /// 上次的缩放模式
    int lastZoomMode() const;
    void setLastZoomMode(int mode);

    /// 上次的显示模式
    int lastDisplayMode() const;
    void setLastDisplayMode(int mode);

    /// 上次是否启用连续滚动
    bool lastContinuousScroll() const;
    void setLastContinuousScroll(bool enabled);

    bool lastNavigation() const;
    void setNavigation(bool enabled);

    // ========== 调试配置 ==========

    /// 是否启用调试输出
    bool debugMode() const { return m_debugMode; }
    void setDebugMode(bool enabled);

    /**
     * @brief 加载配置
     */
    void load();

    /**
     * @brief 保存配置
     */
    void save();

    /**
     * @brief 重置为默认配置
     */
    void resetToDefaults();

private:
    AppConfig();
    ~AppConfig();

    // 禁用拷贝
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    void loadDefaults();

private:
    QSettings m_settings;

    // 缓存配置
    int m_maxCacheSize;
    int m_preloadMargin;

    // 性能配置
    int m_resizeDebounceDelay;

    // UI配置
    QColor m_backgroundColor;
    QSize m_defaultWindowSize;

    // 用户偏好
    bool m_rememberLastFile;
    bool m_debugMode;
};

#endif // APPCONFIG_H
