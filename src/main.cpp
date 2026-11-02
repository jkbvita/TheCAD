#include "ui/MainWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <Standard_Failure.hxx>
#include <exception>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("TheCAD");
    QApplication::setApplicationVersion("0.1.0");
    try {
        thecad::ui::MainWindow window;
        window.showMaximized();
        return application.exec();
    } catch (const Standard_Failure& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.GetMessageString()));
    } catch (const std::exception& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.what()));
    }
    return 1;
}
