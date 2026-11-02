#include "modeling/Box.h"
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
        const auto box = thecad::modeling::makeBox(100.0, 70.0, 40.0);
        thecad::ui::MainWindow window(box);
        window.show();
        return application.exec();
    } catch (const Standard_Failure& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.GetMessageString()));
    } catch (const std::exception& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.what()));
    }
    return 1;
}
