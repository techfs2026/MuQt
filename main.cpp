#include "mainwindow.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/icons/icons/windows.png"));

    // 创建主窗口
    MainWindow mainWindow;
    mainWindow.show();

    int result = app.exec();

    return result;
}
