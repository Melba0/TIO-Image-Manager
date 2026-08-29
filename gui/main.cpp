#include "MainWindow.h"
#include "Logger.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TioDSL");
    QApplication::setApplicationName("dsl_qt");
    QApplication::setApplicationVersion("1.0");

    Logger::instance();       // create before installing the handler
    Logger::install();

    // Prefer the embedded resource icon; fall back to icon.ico next to the exe.
    QIcon icon(":/icon.ico");
    if (icon.isNull()) {
        icon = QIcon(QCoreApplication::applicationDirPath() + "/icon.ico");
        if (icon.isNull()) {
            icon = QIcon(QCoreApplication::applicationDirPath() + "/../icon.ico");
        }
    }
    if (!icon.isNull()) app.setWindowIcon(icon);

    MainWindow w;
    w.show();
    return app.exec();
}