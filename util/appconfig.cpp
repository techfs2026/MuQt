#include "appconfig.h"
#include <QApplication>

AppConfig::AppConfig()
    : m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QApplication::organizationName(),
                 QApplication::applicationName())
{
    loadDefaults();
    load();
}

AppConfig::~AppConfig()
{
    save();
}

AppConfig& AppConfig::instance()
{
    static AppConfig instance;
    return instance;
}

void AppConfig::loadDefaults()
{
    // 缓存配置默认值
    m_maxCacheSize = 10;
    m_preloadMargin = 500;

    // 性能配置默认值
    m_resizeDebounceDelay = 150;

    // UI配置默认值
    m_backgroundColor = QColor(64, 64, 64);
    m_defaultWindowSize = QSize(1300, 800);

    // 用户偏好默认值
    m_rememberLastFile = true;
    m_debugMode = false;
}

void AppConfig::load()
{
    return;
    // 加载缓存配置
    m_maxCacheSize = m_settings.value("Cache/MaxSize", m_maxCacheSize).toInt();
    m_preloadMargin = m_settings.value("Cache/PreloadMargin", m_preloadMargin).toInt();

    // 加载性能配置
    m_resizeDebounceDelay = m_settings.value("Performance/ResizeDebounceDelay",
                                             m_resizeDebounceDelay).toInt();

    // 加载UI配置
    m_backgroundColor = m_settings.value("UI/BackgroundColor",
                                         m_backgroundColor).value<QColor>();
    m_defaultWindowSize = m_settings.value("UI/DefaultWindowSize",
                                           m_defaultWindowSize).toSize();

    // 加载用户偏好
    m_rememberLastFile = m_settings.value("Preferences/RememberLastFile",
                                          m_rememberLastFile).toBool();
    m_debugMode = m_settings.value("Debug/Enabled", m_debugMode).toBool();
}

void AppConfig::save()
{
    return;
    // 保存缓存配置
    m_settings.setValue("Cache/MaxSize", m_maxCacheSize);
    m_settings.setValue("Cache/PreloadMargin", m_preloadMargin);

    // 保存性能配置
    m_settings.setValue("Performance/ResizeDebounceDelay", m_resizeDebounceDelay);

    // 保存UI配置
    m_settings.setValue("UI/BackgroundColor", m_backgroundColor);
    m_settings.setValue("UI/DefaultWindowSize", m_defaultWindowSize);

    // 保存用户偏好
    m_settings.setValue("Preferences/RememberLastFile", m_rememberLastFile);
    m_settings.setValue("Debug/Enabled", m_debugMode);

    m_settings.sync();
}

void AppConfig::resetToDefaults()
{
    m_settings.clear();
    loadDefaults();
    save();
}

void AppConfig::setMaxCacheSize(int size)
{
    if (size > 0 && size <= 100) {
        m_maxCacheSize = size;
    }
}

void AppConfig::setPreloadMargin(int margin)
{
    if (margin >= 0 && margin <= 2000) {
        m_preloadMargin = margin;
    }
}

void AppConfig::setResizeDebounceDelay(int delay)
{
    if (delay >= 0 && delay <= 1000) {
        m_resizeDebounceDelay = delay;
    }
}

void AppConfig::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
}

void AppConfig::setDefaultWindowSize(const QSize& size)
{
    m_defaultWindowSize = size;
}

void AppConfig::setRememberLastFile(bool remember)
{
    m_rememberLastFile = remember;
}

QString AppConfig::lastFilePath() const
{
    return m_settings.value("LastSession/FilePath").toString();
}

void AppConfig::setLastFilePath(const QString& path)
{
    m_settings.setValue("LastSession/FilePath", path);
}

int AppConfig::lastZoomMode() const
{
    return m_settings.value("LastSession/ZoomMode", 2).toInt(); // 默认FitWidth
}

void AppConfig::setLastZoomMode(int mode)
{
    m_settings.setValue("LastSession/ZoomMode", mode);
}

int AppConfig::lastDisplayMode() const
{
    return m_settings.value("LastSession/DisplayMode", 0).toInt(); // 默认SinglePage
}

void AppConfig::setLastDisplayMode(int mode)
{
    m_settings.setValue("LastSession/DisplayMode", mode);
}

bool AppConfig::lastContinuousScroll() const
{
    return m_settings.value("LastSession/ContinuousScroll", false).toBool();
}

void AppConfig::setLastContinuousScroll(bool enabled)
{
    m_settings.setValue("LastSession/ContinuousScroll", enabled);
}

void AppConfig::setDebugMode(bool enabled)
{
    m_debugMode = enabled;
}

bool AppConfig::lastNavigation() const {
    return m_settings.value("LastSession/ShowNavigation", false).toBool();
}

void AppConfig::setNavigation(bool enabled) {
    m_settings.setValue("LastSession/ShowNavigation", enabled);
}
