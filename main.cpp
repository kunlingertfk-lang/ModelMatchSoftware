#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QFont font("Microsoft YaHei", 9);
    QApplication::setFont(font);
    MainWindow w;
    w.show();
    return a.exec();
}
