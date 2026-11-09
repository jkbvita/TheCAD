#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <Standard_Failure.hxx>
#include <exception>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("TheCAD");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/branding/thecad-mark.png"));
    // A deployed folder carries OCCT resources next to the executable.
    // Development startup uses the environment helper when this directory is absent.
    const QString shaderDirectory = QCoreApplication::applicationDirPath() + "/resources/Shaders";
    if (QDir(shaderDirectory).exists()) {
        qputenv("CSF_ShadersDirectory", QDir::toNativeSeparators(shaderDirectory).toUtf8());
    }
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
