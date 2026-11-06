#include "ui/MainWindow.h"
#include "exchange/ShapeExporter.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <Standard_Failure.hxx>
#include <exception>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("TheCAD");
    QApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("TheCAD parametric CAD application");
    parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument("part","Saved .thepart or legacy .tcpart file to open.","[part]");
    parser.process(application);
    const auto files=parser.positionalArguments();
    if(files.size()>1){QMessageBox::critical(nullptr,"TheCAD startup error","Open one part at a time. Drop a single saved part onto the TheCAD shortcut.");return 2;}
    // A deployed folder carries OCCT resources next to the executable.
    // Development startup uses the environment helper when this directory is absent.
    const QString shaderDirectory = QCoreApplication::applicationDirPath() + "/resources/Shaders";
    if (QDir(shaderDirectory).exists()) {
        qputenv("CSF_ShadersDirectory", QDir::toNativeSeparators(shaderDirectory).toUtf8());
    }
    thecad::exchange::configureResources(QCoreApplication::applicationDirPath()+"/resources");
    try {
        thecad::ui::MainWindow window;
        window.showMaximized();
        if(!files.isEmpty())QTimer::singleShot(0,&window,[&application,&window,path=files.front()]{if(!window.openPartFile(path))application.exit(1);});
        return application.exec();
    } catch (const Standard_Failure& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.GetMessageString()));
    } catch (const std::exception& failure) {
        QMessageBox::critical(nullptr, "TheCAD startup error", QString::fromUtf8(failure.what()));
    }
    return 1;
}
