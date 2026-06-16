#include "mainwindow.h"
#include "stylemanager.h"
#include "appconfig.h"
#include "outlineeditor.h"

#include <QApplication>
#include <QMessageBox>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifdef Q_OS_MACOS
#include <QFileOpenEvent>
#include <QStringList>
#include <QUrl>
#include <utility>

// macOS 通过 Apple Event（而非 argv）投递文件打开请求：
// 在「访达」中双击 PDF、或选择「打开方式 → OwlPDF」时，
// 系统会向 QApplication 发送 QFileOpenEvent。
// 此过滤器捕获该事件并转交主窗口打开；若窗口尚未就绪则先缓存。
class MacFileOpenFilter : public QObject
{
public:
    void setWindow(MainWindow* window)
    {
        m_window = window;
        for (const QString& path : std::as_const(m_pending)) {
            m_window->openFileFromCommandLine(path);
        }
        m_pending.clear();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            auto* openEvent = static_cast<QFileOpenEvent*>(event);
            QString path = openEvent->file();
            if (path.isEmpty() && openEvent->url().isLocalFile()) {
                path = openEvent->url().toLocalFile();
            }
            if (!path.isEmpty()) {
                if (m_window) {
                    m_window->openFileFromCommandLine(path);
                } else {
                    m_pending.append(path);  // 窗口未就绪，先缓存
                }
            }
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    MainWindow* m_window = nullptr;
    QStringList m_pending;
};
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setOrganizationName("OwlPDF");
    app.setApplicationName("OwlPDF");
#ifdef Q_OS_WIN
    app.setWindowIcon(QIcon(":/resources/windows.ico"));
#endif

    // 启动时清理一次目录备份（按时间/总体积），依赖上面的应用名设置 AppDataLocation
    OutlineEditor::cleanupBackups();

#ifdef Q_OS_MACOS
    // 尽早安装，以捕获启动阶段（冷启动打开文件）投递的 QFileOpenEvent
    MacFileOpenFilter macFileOpenFilter;
    app.installEventFilter(&macFileOpenFilter);
#endif

    StyleManager::instance().initialize();
    StyleManager::instance().setTheme("light");
    StyleManager::instance().applyStyleToApplication(&app);

    const QLocale systemLocale = QLocale::system();

    // Qt 自带控件的标准文案（QMessageBox 的 Yes/No、QDialogButtonBox 的
    // OK/Cancel/Restore Defaults 等）来自 Qt 翻译包。部署后优先从应用包内加载，
    // 开发期再回退到 Qt 安装目录；最后安装应用自己的翻译。
    static QTranslator qtBaseTranslator;
    static QTranslator qtTranslator;
    const QStringList translationDirs = {
        QCoreApplication::applicationDirPath() + "/translations",
        QCoreApplication::applicationDirPath() + "/../Resources/translations",
        QLibraryInfo::path(QLibraryInfo::TranslationsPath)
    };

    auto loadQtTranslation = [&](QTranslator& target, const QString& baseName) {
        for (const QString& dir : translationDirs) {
            const QString cleanDir = QDir::cleanPath(dir);
            if (QFileInfo::exists(cleanDir)
                && target.load(systemLocale, baseName, "_", cleanDir)) {
                app.installTranslator(&target);
                return true;
            }
        }
        return false;
    };

    if (!loadQtTranslation(qtBaseTranslator, QStringLiteral("qtbase"))) {
        loadQtTranslation(qtTranslator, QStringLiteral("qt"));
    } else {
        loadQtTranslation(qtTranslator, QStringLiteral("qt"));
    }

    static QTranslator translator;
    if (translator.load(":/translations/owlpdf_" + systemLocale.name())) {
        app.installTranslator(&translator);
    }

    MainWindow mainWindow;
    mainWindow.show();

#ifdef Q_OS_WIN
    QTimer::singleShot(500, &mainWindow, [&mainWindow]() {
        mainWindow.checkAndShowFirstRunDialog();
    });
#endif

#ifdef Q_OS_MACOS
    // 窗口就绪，转交此前缓存的文件打开请求
    macFileOpenFilter.setWindow(&mainWindow);
#endif

    if (argc > 1) {
        // 命令行指定了文件 → 只开它，不恢复上次会话
        QString filePath = QString::fromLocal8Bit(argv[1]);
        QTimer::singleShot(100, &mainWindow, [&mainWindow, filePath]() {
            mainWindow.openFileFromCommandLine(filePath);
        });
    } else if (AppConfig::instance().rememberLastFile()) {
        // 未指定文件 → 恢复上次会话。延后一拍，让 macOS 的 QFileOpenEvent
        // （双击打开）有机会先到；restoreLastSession 内部若发现已有文档会自动跳过。
        QTimer::singleShot(120, &mainWindow, [&mainWindow]() {
            mainWindow.restoreLastSession();
        });
    }

    int result = app.exec();

    return result;
}
