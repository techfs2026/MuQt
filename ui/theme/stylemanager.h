#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QString>
#include <QMap>
#include <QColor>
#include <QObject>

/**
 * @brief 主题配置结构
 *
 * 存储主题的核心颜色和属性
 */
struct ThemeConfig {
    // 主色调
    QColor primaryColor;           // 主色
    QColor secondaryColor;         // 辅助色
    QColor accentColor;            // 强调色

    // 背景色
    QColor backgroundColor;        // 主背景
    QColor surfaceColor;           // 表面/卡片背景
    QColor paperColor;             // 纸张背景

    // 文本色
    QColor textPrimary;            // 主要文本
    QColor textSecondary;          // 次要文本
    QColor textDisabled;           // 禁用文本

    // 边框色
    QColor borderLight;            // 浅色边框
    QColor borderMedium;           // 中等边框
    QColor borderDark;             // 深色边框

    // 交互状态
    QColor hoverBackground;        // 悬停背景
    QColor pressedBackground;      // 按下背景
    QColor selectedBackground;     // 选中背景

    // 特殊色
    QColor successColor;           // 成功
    QColor warningColor;           // 警告
    QColor errorColor;             // 错误
    QColor infoColor;              // 信息

    // 主题属性
    QString name;                  // 主题名称
    bool isDark;                   // 是否为深色主题
    int borderRadius;              // 默认圆角
    int fontSize;                  // 默认字体大小
    QString fontFamily;            // 字体族
};

/**
 * @brief 样式管理器 - 单例模式
 *
 * 职责：
 * 1. 管理主题切换（Light、Dark等）
 * 2. 加载和缓存QSS文件（主题样式、组件样式）
 * 3. 提供主题颜色访问接口
 * 4. 支持样式热重载（开发调试用）
 * 5. 处理样式变量替换
 */
class StyleManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static StyleManager& instance();

    /**
     * @brief 初始化样式系统
     */
    void initialize();

    /**
     * @brief 设置当前主题
     * @param themeName 主题名称（如 "light", "dark"）
     * @return 是否成功
     */
    bool setTheme(const QString& themeName);

    /**
     * @brief 获取当前主题名称
     */
    QString currentTheme() const { return m_currentTheme; }

    /**
     * @brief 获取当前主题配置
     */
    ThemeConfig currentConfig() const { return m_currentConfig; }

    /**
     * @brief 应用样式到应用程序
     * @param app QApplication指针
     */
    void applyStyleToApplication(QObject* app);

    /**
     * @brief 应用样式到特定组件
     * @param widget 组件指针
     * @param componentName 组件样式名称（可选）
     */
    void applyStyleToWidget(QWidget* widget, const QString& componentName = QString());

    /**
     * @brief 获取完整样式表（主题 + 所有组件）
     */
    QString getFullStyleSheet();

    /**
     * @brief 获取主题样式表
     */
    QString getThemeStyleSheet();

    /**
     * @brief 获取组件样式表
     * @param componentName 组件名称（如 "mainwindow", "navigationpanel"）
     */
    QString getComponentStyleSheet(const QString& componentName);

    /**
     * @brief 重新加载样式（开发调试用）
     */
    void reloadStyles();

    /**
     * @brief 注册自定义主题
     */
    void registerTheme(const QString& themeName, const ThemeConfig& config);

    /**
     * @brief 获取可用主题列表
     */
    QStringList availableThemes() const;

    /**
     * @brief 是否为深色主题
     */
    bool isDarkTheme() const { return m_currentConfig.isDark; }

    /**
     * @brief 获取主题颜色
     */
    QColor getColor(const QString& colorName) const;

signals:
    /**
     * @brief 主题已更改
     */
    void themeChanged(const QString& themeName);

private:
    StyleManager();
    ~StyleManager();
    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

    /**
     * @brief 加载内置主题
     */
    void loadBuiltInThemes();

    /**
     * @brief 加载主题配置
     */
    ThemeConfig loadThemeConfig(const QString& themeName);

    /**
     * @brief 加载QSS文件
     * @param filePath 文件路径（支持资源路径 :/ 和绝对路径）
     */
    QString loadStyleSheetFile(const QString& filePath);

    /**
     * @brief 处理样式变量替换
     * @param styleSheet 原始样式表
     * @param config 主题配置
     */
    QString processVariables(const QString& styleSheet, const ThemeConfig& config);

    /**
     * @brief 颜色转十六进制字符串
     */
    QString colorToHex(const QColor& color) const;

    QString generateBasicThemeStyle() const;

private:
    QString m_currentTheme;
    ThemeConfig m_currentConfig;
    QMap<QString, ThemeConfig> m_themes;
    QMap<QString, QString> m_cachedStyleSheets;  // 缓存的样式表
    QString m_styleResourcePath;  // 样式资源路径
};

#endif // STYLEMANAGER_H
