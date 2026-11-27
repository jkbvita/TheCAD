#pragma once
#include <QSettings>
#include <QString>
#include <optional>
#include <vector>

namespace thecad::application {
enum class LaunchDocument { Part, Assembly };
std::optional<LaunchDocument> documentTypeForPath(const QString& path);
struct RecentEntry { QString path; bool pinned = false; };

// Preferences only: no document loading, geometry, or widget dependencies.
// Settings are injected so tests never touch the owner's recent-file history.
class RecentDocuments {
public:
    explicit RecentDocuments(QSettings& settings) : m_settings(settings) {}
    std::vector<RecentEntry> documents() const;
    std::vector<RecentEntry> folders() const;
    void recordDocument(const QString& path);
    void addFolder(const QString& path);
    void setPinned(const QString& path, bool folder, bool pinned);
    void remove(const QString& path, bool folder);
    void clearUnpinned(bool folder);
private:
    std::vector<RecentEntry> read(bool folder) const;
    void write(bool folder, std::vector<RecentEntry> entries);
    void touch(const QString& path, bool folder, bool pin = false);
    QSettings& m_settings;
};
void recordRecentDocument(const QString& path);
} // namespace thecad::application
