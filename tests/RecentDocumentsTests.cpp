#include "application/RecentDocuments.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace thecad::application;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void create(const QString& path) { QFile file(path); require(file.open(QIODevice::WriteOnly), "Cannot create fixture"); file.write("fixture"); }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir folder; require(folder.isValid(), "Temporary folder unavailable");
        const auto part = folder.filePath(QString::fromUtf8("část.THEPART"));
        const auto assembly = folder.filePath("two parts.theasmb"); create(part); create(assembly);
        require(documentTypeForPath(part) == LaunchDocument::Part, "Part routing");
        require(documentTypeForPath("old.TCPART") == LaunchDocument::Part, "Legacy routing");
        require(documentTypeForPath(assembly) == LaunchDocument::Assembly, "Assembly routing");
        require(!documentTypeForPath("drawing.thedrw") && !documentTypeForPath("export.stl"), "Unsupported route advertised");
        const auto settingsPath = folder.filePath("recent.ini");
        QSettings settings(settingsPath, QSettings::IniFormat); RecentDocuments recent(settings);
        recent.recordDocument(part); recent.recordDocument(assembly); recent.recordDocument(part.toLower());
        require(recent.documents().size() == 2, "Case-insensitive deduplication failed");
        require(recent.folders().size() == 1, "Folder deduplication failed");
        recent.setPinned(assembly, false, true);
        require(recent.documents().front().pinned && recent.documents().front().path == assembly, "Pin ordering");
        recent.addFolder(folder.path()); require(recent.folders().front().pinned, "Selected folders must be pinned");
        recent.recordDocument(folder.filePath("missing.thepart"));
        const auto stl = folder.filePath("export.stl"); create(stl); recent.recordDocument(stl);
        require(recent.documents().size() == 2, "Unavailable or unsupported files added");
        QSettings reopened(settingsPath,QSettings::IniFormat);
        require(RecentDocuments(reopened).documents().size() == 2 && RecentDocuments(reopened).documents().front().pinned, "History did not persist");
        for(int i=0;i<60;++i) { const auto path=folder.filePath(QString("model%1.thepart").arg(i)); create(path); recent.recordDocument(path); }
        require(recent.documents().size() == 51 && recent.documents().front().pinned, "History cap lost pinned file");
        recent.clearUnpinned(false); require(recent.documents().size() == 1, "Clear unpinned removed pinned file");
        recent.remove(assembly,false); require(recent.documents().empty() && QFile::exists(assembly), "Remove must only remove history");
        recent.clearUnpinned(true); require(recent.folders().size()==1,"Pinned folder lost");
        recent.setPinned(folder.path(),true,false); recent.clearUnpinned(true); require(recent.folders().empty(),"Folder unpin/clear failed");
        std::cout << "PASS: routing, recent-file persistence, deduplication, pinning, bounds and safe removal\n"; return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
