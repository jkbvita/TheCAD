#include "application/RecentDocuments.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace thecad::application {
namespace {
QString normalized(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool same(const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) == 0; }
QString key(bool folder) { return folder ? "welcome/folders" : "welcome/documents"; }
}
std::optional<LaunchDocument> documentTypeForPath(const QString& path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "thepart" || suffix == "tcpart") return LaunchDocument::Part;
    if (suffix == "theasmb") return LaunchDocument::Assembly;
    return {};
}
std::vector<RecentEntry> RecentDocuments::read(bool folder) const {
    std::vector<RecentEntry> entries;
    for (const auto& value : m_settings.value(key(folder)).toList()) {
        const auto data = value.toMap(); const auto path = data.value("path").toString();
        if (path.isEmpty() || (!folder && !documentTypeForPath(path))) continue;
        const auto canonical = normalized(path);
        if (std::none_of(entries.begin(), entries.end(), [&](const auto& e) { return same(e.path, canonical); }))
            entries.push_back({canonical, data.value("pinned").toBool()});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.pinned > b.pinned; });
    return entries;
}
std::vector<RecentEntry> RecentDocuments::documents() const { return read(false); }
std::vector<RecentEntry> RecentDocuments::folders() const { return read(true); }
void RecentDocuments::write(bool folder, std::vector<RecentEntry> entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.pinned > b.pinned; });
    QVariantList values; int unpinned = 0;
    for (const auto& entry : entries) {
        if (!entry.pinned && ++unpinned > (folder ? 20 : 50)) continue;
        values.append(QVariantMap{{"path", entry.path}, {"pinned", entry.pinned}});
    }
    m_settings.setValue(key(folder), values); m_settings.sync();
}
void RecentDocuments::touch(const QString& path, bool folder, bool pin) {
    const auto canonical = normalized(path); auto entries = read(folder);
    for (const auto& e : entries) if (same(e.path, canonical)) pin = pin || e.pinned;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e) { return same(e.path, canonical); }), entries.end());
    entries.insert(entries.begin(), {canonical, pin}); write(folder, std::move(entries));
}
void RecentDocuments::recordDocument(const QString& path) {
    if (!documentTypeForPath(path) || !QFileInfo(path).isFile()) return;
    touch(path, false); touch(QFileInfo(path).absolutePath(), true);
}
void RecentDocuments::addFolder(const QString& path) { if (QFileInfo(path).isDir()) touch(path, true, true); }
void RecentDocuments::setPinned(const QString& path, bool folder, bool pinned) {
    auto entries = read(folder); for (auto& e : entries) if (same(e.path, normalized(path))) e.pinned = pinned;
    write(folder, std::move(entries));
}
void RecentDocuments::remove(const QString& path, bool folder) {
    auto entries = read(folder);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e) { return same(e.path, normalized(path)); }), entries.end());
    write(folder, std::move(entries));
}
void RecentDocuments::clearUnpinned(bool folder) {
    auto entries = read(folder);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto& e) { return !e.pinned; }), entries.end());
    write(folder, std::move(entries));
}
void recordRecentDocument(const QString& path) { QSettings settings("TheCAD", "TheCAD"); RecentDocuments(settings).recordDocument(path); }
} // namespace thecad::application
