#include <QApplication>

#include "app/appinfo.h"
#include "app/mainwindow.h"
#include "ui/theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Tên hiện trên giao diện. Tên file chạy và file cấu hình là chuyện khác,
    // xem src/appinfo.h.
    QCoreApplication::setOrganizationName(appinfo::organizationName());
    QCoreApplication::setApplicationName(appinfo::displayName());

    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(theme::styleSheet());

    MainWindow window;
    window.showMaximized();

    return app.exec();
}
