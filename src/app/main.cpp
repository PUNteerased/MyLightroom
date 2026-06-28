#include "MainWindow.hpp"
#include <QApplication>
#include <QIcon>
#include <QLocale>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    QApplication::setApplicationName(QStringLiteral("MyLightroom"));
    QApplication::setOrganizationName(QStringLiteral("MyLightroom"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/LrC.png")));

    mylr::MainWindow window;
    window.show();

    return app.exec();
}
