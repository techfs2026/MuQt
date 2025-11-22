#include "mainwindow.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 设置应用信息（用于QSettings）
    QApplication::setOrganizationName("SimplePDFViewer");
    QApplication::setOrganizationDomain("example.com");
    QApplication::setApplicationName("SimplePDFViewer");
    QApplication::setApplicationVersion("1.0.0");

    // 创建主窗口
    MainWindow mainWindow;
    mainWindow.show();

    int result = app.exec();

    return result;
}
