#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <vector>
#include <optional>
#include <string>
#include <QPixmap>
#include <QIcon>
#include <QHash>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QFile>

struct TrackedWork {
    int id = -1;
    QString series_path;       // Canonical directory path of the series
    QString title;             // Verified series title
    int anilist_id = 0;        // AniList media ID (0 if unlinked)
    QString anilist_title;     // Matched AniList title
    QString floppy_id;         // Floppy media ID
    QString cover_url;         // Cover image URL or local path
    bool is_tracking = false;  // Explicit opt-in! False by default
    int last_volume = 0;       // Last tracked volume
    float last_chapter = 0.0f; // Last tracked chapter
    QString last_read_file;    // Exact file path last opened in this series
    qint64 last_read_time = 0; // Unix epoch ms of last reading activity
    QString reading_status = QStringLiteral("CURRENT"); // CURRENT, COMPLETED, PAUSED, PLANNING, DROPPED
    int total_volumes = 0;     // Total volumes (from AniList)
    int total_chapters = 0;    // Total chapters (from AniList)

    QString status_badge() const {
        if (!is_tracking) {
            return QStringLiteral("[Untracked]");
        }
        if (reading_status == QLatin1String("COMPLETED")) {
            return QStringLiteral("[Completed]");
        } else if (reading_status == QLatin1String("PAUSED")) {
            return QStringLiteral("[On Hold]");
        } else if (reading_status == QLatin1String("PLANNING")) {
            return QStringLiteral("[Plan to Read]");
        } else if (reading_status == QLatin1String("DROPPED")) {
            return QStringLiteral("[Dropped]");
        }
        return QStringLiteral("[Reading]");
    }
};

struct ParsedWorkInfo {
    QString series_path;
    QString title;
    int volume = 0;
    float chapter = 0.0f;
};

struct AniListCandidate {
    int id = 0;
    QString title_english;
    QString title_romaji;
    QString cover_url;
    int total_volumes = 0;
    int total_chapters = 0;
    QString status;

    QString display_title() const {
        if (!title_english.isEmpty()) return title_english;
        return title_romaji;
    }

    QString details_str() const {
        QString s = QString("AniList #%1").arg(id);
        if (total_volumes > 0) {
            s += QString(" | %1 Vols").arg(total_volumes);
        }
        if (total_chapters > 0) {
            s += QString(" | %1 Chs").arg(total_chapters);
        }
        if (!status.isEmpty()) {
            s += QString(" (%1)").arg(status);
        }
        return s;
    }
};

class TrackerManager : public QObject {
    Q_OBJECT

private:
    QNetworkAccessManager* network_manager;
    QHash<QString, QPixmap> pixmap_cache;

public:
    explicit TrackerManager(QNetworkAccessManager* net_mgr, QObject* parent = nullptr);

    // Heuristically extract title, volume, chapter from file/folder path
    static ParsedWorkInfo parse_work_info(const QString& file_path);

    // Search for a local cover image file (cover.jpg, folder.png, etc.)
    static QString find_local_cover(const QString& series_path);

    // Helper to generate a cropped 2:3 aspect ratio cover QIcon from a QPixmap
    static QIcon create_cover_icon(const QPixmap& pm, int w = 32, int h = 44);

    // Download or load from disk/memory cache a cover image
    void fetch_image(const QString& url_or_path, std::function<void(const QPixmap& pixmap)> callback);

    // Query AniList GraphQL API for media details (cover image, volumes, chapters) by media ID
    void fetch_cover_by_anilist_id(int media_id,
                                  std::function<void(const QString& cover_url, const QPixmap& pixmap, int total_vols, int total_chs)> callback);

    // Query AniList GraphQL API for matching manga titles (including covers)
    void search_anilist(const QString& title,
                        std::function<void(bool success, const std::vector<AniListCandidate>& candidates, const QString& error)> callback);

    // Sync volume/chapter progress to AniList
    void sync_anilist_progress(int media_id,
                               int volume,
                               float chapter,
                               const QString& token,
                               std::function<void(bool success, const QString& message)> callback);

    // Sync reading status to AniList
    void sync_anilist_status(int media_id,
                             const QString& status,
                             const QString& token,
                             std::function<void(bool success, const QString& message)> callback);

    // Sync volume/chapter progress to Floppy REST API
    void sync_floppy_progress(const QString& series_title,
                              int volume,
                              float chapter,
                              const QString& floppy_url,
                              const QString& token,
                              std::function<void(bool success, const QString& message)> callback);
};
