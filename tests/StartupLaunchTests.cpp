#include <Windows.h> // Before OCCT headers, which restrict the Windows API surface.
#include "document/PartDocument.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <string>

namespace {
struct WindowTitles {DWORD process;QStringList titles;};
BOOL CALLBACK collectTitles(HWND window,LPARAM parameter){
    auto& result=*reinterpret_cast<WindowTitles*>(parameter);DWORD process=0;GetWindowThreadProcessId(window,&process);
    if(process!=result.process||!IsWindowVisible(window))return TRUE;
    const int length=GetWindowTextLengthW(window);if(length<=0)return TRUE;
    std::wstring title(std::size_t(length)+1,L'\0');const int copied=GetWindowTextW(window,title.data(),length+1);
    if(copied>0)result.titles.append(QString::fromWCharArray(title.data(),copied));return TRUE;
}
QStringList titlesOf(QProcess& process){WindowTitles result{DWORD(process.processId()),{}};EnumWindows(collectTitles,reinterpret_cast<LPARAM>(&result));return result.titles;}
class OwnedApplication {
public:
    QProcess process;
    ~OwnedApplication(){if(process.state()!=QProcess::NotRunning){process.terminate();if(!process.waitForFinished(1500)){process.kill();process.waitForFinished(3000);}}}
};
QString executable(){const auto configured=qEnvironmentVariable("THECAD_TEST_EXECUTABLE");return configured.isEmpty()?QCoreApplication::applicationDirPath()+"/TheCAD.exe":configured;}
QByteArray bytes(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
thecad::document::PartDocument fixture(){
    using namespace thecad;auto part=document::PartDocument::createEmpty();modeling::Sketch sketch;sketch.entities={{modeling::EntityKind::Rectangle,{-25,-20},{25,20},1,"outline"}};const auto profile=part.addSketch(sketch);part.addExtrude(profile,{"outline"},20,modeling::Direction::Positive);
    modeling::Sketch hole;hole.entities={{modeling::EntityKind::Circle,{0,0},{},5,"hole"}};const auto cutter=part.addSketch(hole);part.addCut(cutter,{"hole"},20,true,modeling::Direction::Positive);
    modeling::DatumDefinition reference;reference.position={10,12,30};part.addReferenceGeometry(reference);return part;
}
}
class StartupLaunchTest final:public QObject {
    Q_OBJECT
    void launch(const QString& path,bool relative){
        QVERIFY2(QFileInfo::exists(executable()),qPrintable(executable()));const auto original=bytes(path);QVERIFY(!original.isEmpty());
        OwnedApplication app;app.process.setProgram(executable());app.process.setWorkingDirectory(QFileInfo(path).absolutePath());app.process.setArguments({relative?QFileInfo(path).fileName():QFileInfo(path).absoluteFilePath()});
        if(QFileInfo::exists(QFileInfo(executable()).absolutePath()+"/qt.conf")){auto environment=QProcessEnvironment::systemEnvironment();environment.remove("QT_QPA_PLATFORM_PLUGIN_PATH");environment.remove("QT_PLUGIN_PATH");environment.remove("CSF_ShadersDirectory");const auto system=environment.value("SystemRoot");environment.insert("PATH",system+"/System32;"+system);app.process.setProcessEnvironment(environment);}
        app.process.start();QVERIFY2(app.process.waitForStarted(5000),qPrintable(app.process.errorString()));
        const auto expected=QFileInfo(path).fileName();const auto loaded=[&]{for(const auto& title:titlesOf(app.process))if(title==expected+" - TheCAD")return true;return false;};
        QTRY_VERIFY2_WITH_TIMEOUT(loaded(),qPrintable("File was not opened; window titles: "+titlesOf(app.process).join(" | ")),10000);
        app.process.terminate();QVERIFY2(app.process.waitForFinished(5000),"A successfully opened saved model should close without an unsaved-empty-part prompt.");QCOMPARE(app.process.exitStatus(),QProcess::NormalExit);QCOMPARE(app.process.exitCode(),0);QCOMPARE(bytes(path),original);
    }
private slots:
    void savedModelArgument_data(){QTest::addColumn<bool>("relative");QTest::newRow("absolute-unicode-spaces")<<false;QTest::newRow("relative-unicode-spaces")<<true;}
    void savedModelArgument(){QFETCH(bool,relative);QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath(QString::fromUtf8("saved model díl.THEPART"));const auto part=fixture();part.save(path);const auto loaded=thecad::document::PartDocument::load(path);QCOMPARE(loaded.features().size(),part.features().size());GProp_GProps mass;BRepGProp::VolumeProperties(loaded.shape(),mass);QVERIFY(mass.Mass()>1000);launch(path,relative);}
    void userSavedModelWhenProvided(){const auto path=qEnvironmentVariable("THECAD_TEST_SAVED_PART");if(path.isEmpty())QSKIP("Optional read-only test of the owner's saved model.");try{const auto part=thecad::document::PartDocument::load(path);QVERIFY(!part.shape().IsNull());QVERIFY(!part.features().empty());}catch(const std::exception& error){QFAIL(error.what());}launch(path,false);}
};
QTEST_GUILESS_MAIN(StartupLaunchTest)
#include "StartupLaunchTests.moc"
