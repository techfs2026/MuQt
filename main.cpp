#include "mainwindow.h"
#include "stylemanager.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/resources/windows.ico"));

    StyleManager::instance().initialize();
    StyleManager::instance().setTheme("light");
    StyleManager::instance().applyStyleToApplication(&app);

    MainWindow mainWindow;
    mainWindow.show();

    int result = app.exec();

    return result;
}
