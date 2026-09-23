#include "tracker.h"
#include <QSet>
#include <QDebug>

TrackerManager::TrackerManager(QNetworkAccessManager* net_mgr, QObject* parent)
    : QObject(parent), network_manager(net_mgr) {
}

ParsedWorkInfo TrackerManager::parse_work_info(const QString& file_path) {
    ParsedWorkInfo info;
    QFileInfo fi(file_path);
    info.series_path = fi.dir().canonicalPath();

    QString stem = fi.completeBaseName();

    // Strip bracketed release metadata, e.g. [Digital], (2020), (Official)
    QRegularExpression bracket_re(QStringLiteral(R"(\[[^\]]*\]|\([^\)]*\))"));
    QString cleaned_stem = stem;
    cleaned_stem.remove(bracket_re);
    cleaned_stem = cleaned_stem.trimmed();

    // Volume pattern: e.g. v08, vol 8, volume.08, Vol_08
    QRegularExpression vol_re(QStringLiteral(R"((?:^|[\s_.\-\[(])(?:vol(?:ume)?|v)[.\s_\-]*0*(\d+))"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch vol_match = vol_re.match(cleaned_stem);
    if (vol_match.hasMatch()) {
        info.volume = vol_match.captured(1).toInt();
    }

    // Chapter pattern: e.g. c024, ch 24, chapter.24, Ch_24.5
    QRegularExpression ch_re(QStringLiteral(R"((?:^|[\s_.\-\[(])(?:ch(?:apter)?|c)[.\s_\-]*0*(\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch ch_match = ch_re.match(cleaned_stem);
    if (ch_match.hasMatch()) {
        info.chapter = ch_match.captured(1).toFloat();
    }

    // Detect Series Title
    QString parent_folder = fi.dir().dirName();
    static const QSet<QString> generic_dirs = {
        QStringLiteral("downloads"), QStringLiteral("manga"), QStringLiteral("comics"),
        QStringLiteral("books"), QStringLiteral("desktop"), QStringLiteral("documents"),
        QStringLiteral("temp"), QStringLiteral("tmp"), QStringLiteral("home"),
        QStringLiteral("media"), QStringLiteral("run"), QStringLiteral("var")
    };

    QRegularExpression vol_dir_re(QStringLiteral(R"(^(?:vol(?:ume)?|v|chapter|c)[\s_.\-]*\d+$)"), QRegularExpression::CaseInsensitiveOption);
    bool parent_is_vol_dir = vol_dir_re.match(parent_folder).hasMatch();

    if (parent_is_vol_dir) {
        QDir parent_dir = fi.dir();
        parent_dir.cdUp();
        QString grand_name = parent_dir.dirName();
        if (!generic_dirs.contains(grand_name.toLower()) && !grand_name.isEmpty()) {
            info.title = grand_name;
        }
    } else if (!generic_dirs.contains(parent_folder.toLower()) && !parent_folder.isEmpty()) {
        info.title = parent_folder;
    }

    // If parent directory was generic, deduce series title from filename
    if (info.title.isEmpty()) {
        QString title_candidate = cleaned_stem;
        // Remove trailing volume/chapter patterns and numbers
        QRegularExpression trim_tail_re(QStringLiteral(R"([\s_.\-\[(]+(?:vol(?:ume)?|v|ch(?:apter)?|c)?[.\s_\-]*\d+.*$)"), QRegularExpression::CaseInsensitiveOption);
        title_candidate.remove(trim_tail_re);

        title_candidate.replace(QLatin1Char('_'), QLatin1Char(' '));
        title_candidate = title_candidate.trimmed();
        // Remove trailing dashes or punctuation
        while (!title_candidate.isEmpty() && (title_candidate.endsWith(QLatin1Char('-')) || title_candidate.endsWith(QLatin1Char('.')))) {
            title_candidate.chop(1);
            title_candidate = title_candidate.trimmed();
        }
        info.title = title_candidate;
    }

    // Fallback if still empty
    if (info.title.isEmpty()) {
        info.title = stem;
    }

    return info;
}

QString TrackerManager::find_local_cover(const QString& series_path) {
    if (series_path.isEmpty()) return QString();
    QDir dir(series_path);
    static const QStringList cover_names = {
        QStringLiteral("cover.jpg"), QStringLiteral("cover.jpeg"), QStringLiteral("cover.png"), QStringLiteral("cover.webp"),
        QStringLiteral("folder.jpg"), QStringLiteral("folder.png"), QStringLiteral("poster.jpg"), QStringLiteral("poster.png")
    };
    for (const QString& name : cover_names) {
        if (dir.exists(name)) {
            return dir.filePath(name);
        }
    }
    return QString();
}

QIcon TrackerManager::create_cover_icon(const QPixmap& pm, int w, int h) {
    if (pm.isNull()) return QIcon();
    QPixmap scaled = pm.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    int x = qMax(0, (scaled.width() - w) / 2);
    int y = qMax(0, (scaled.height() - h) / 2);
    QPixmap cropped = scaled.copy(x, y, w, h);
    return QIcon(cropped);
}

void TrackerManager::fetch_image(const QString& url_or_path, std::function<void(const QPixmap& pixmap)> callback) {
    if (url_or_path.trimmed().isEmpty()) {
        if (callback) callback(QPixmap());
        return;
    }

    QString trimmed = url_or_path.trimmed();

    if (pixmap_cache.contains(trimmed)) {
        if (callback) callback(pixmap_cache.value(trimmed));
        return;
    }

    if (trimmed.startsWith(QLatin1String("/")) || trimmed.startsWith(QLatin1String("file://"))) {
        QString local_file = trimmed;
        if (local_file.startsWith(QLatin1String("file://"))) {
            local_file = QUrl(local_file).toLocalFile();
        }
        if (QFile::exists(local_file)) {
            QPixmap pm(local_file);
            if (!pm.isNull()) {
                pixmap_cache.insert(trimmed, pm);
                if (callback) callback(pm);
                return;
            }
        }
        if (callback) callback(QPixmap());
        return;
    }

    QString cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache_root.isEmpty()) {
        cache_root = QDir::homePath() + QStringLiteral("/.cache/sioyek");
    }
    QString covers_dir = cache_root + QStringLiteral("/covers");
    QDir().mkpath(covers_dir);

    QByteArray hash = QCryptographicHash::hash(trimmed.toUtf8(), QCryptographicHash::Sha1).toHex();
    QString disk_path = covers_dir + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");

    if (QFile::exists(disk_path)) {
        QPixmap pm(disk_path);
        if (!pm.isNull()) {
            pixmap_cache.insert(trimmed, pm);
            if (callback) callback(pm);
            return;
        }
    }

    if (!network_manager) {
        if (callback) callback(QPixmap());
        return;
    }

    QUrl req_url(trimmed);
    QNetworkRequest request(req_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sioyek/2.0"));
    QNetworkReply* reply = network_manager->get(request);
    reply->setProperty("sioyek_network_request_type", "cover_download");

    QObject::connect(reply, &QNetworkReply::finished, [this, reply, trimmed, disk_path, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(QPixmap());
            return;
        }

        QByteArray data = reply->readAll();
        QPixmap pm;
        if (pm.loadFromData(data)) {
            QFile f(disk_path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
                f.close();
            }
            pixmap_cache.insert(trimmed, pm);
            if (callback) callback(pm);
        } else {
            if (callback) callback(QPixmap());
        }
    });
}

void TrackerManager::fetch_cover_by_anilist_id(int media_id,
                                              std::function<void(const QString& cover_url, const QPixmap& pixmap, int total_vols, int total_chs)> callback) {
    if (!network_manager || media_id <= 0) {
        if (callback) callback(QString(), QPixmap(), 0, 0);
        return;
    }

    QUrl url(QStringLiteral("https://graphql.anilist.co"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    QJsonObject variables;
    variables.insert(QStringLiteral("id"), media_id);

    QString query_str = QStringLiteral(
        "query ($id: Int) { "
        "  Media(id: $id, type: MANGA) { "
        "    id "
        "    volumes "
        "    chapters "
        "    coverImage { large medium } "
        "  } "
        "}"
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("query"), query_str);
    payload.insert(QStringLiteral("variables"), variables);

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = network_manager->post(request, body);
    reply->setProperty("sioyek_network_request_type", "anilist_cover");

    QObject::connect(reply, &QNetworkReply::finished, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(QString(), QPixmap(), 0, 0);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parse_err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parse_err);
        if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
            if (callback) callback(QString(), QPixmap(), 0, 0);
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject media_obj = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("Media")).toObject();
        int total_vols = media_obj.value(QStringLiteral("volumes")).toInt();
        int total_chs = media_obj.value(QStringLiteral("chapters")).toInt();

        QJsonObject cover_obj = media_obj.value(QStringLiteral("coverImage")).toObject();
        QString cover_url = cover_obj.value(QStringLiteral("large")).toString();
        if (cover_url.isEmpty()) {
            cover_url = cover_obj.value(QStringLiteral("medium")).toString();
        }

        if (cover_url.isEmpty()) {
            if (callback) callback(QString(), QPixmap(), total_vols, total_chs);
            return;
        }

        fetch_image(cover_url, [callback, cover_url, total_vols, total_chs](const QPixmap& pm) {
            if (callback) callback(cover_url, pm, total_vols, total_chs);
        });
    });
}

void TrackerManager::search_anilist(const QString& title,
                                    std::function<void(bool success, const std::vector<AniListCandidate>& candidates, const QString& error)> callback) {
    if (!network_manager) {
        if (callback) callback(false, {}, QStringLiteral("Network manager unavailable"));
        return;
    }

    QUrl url(QStringLiteral("https://graphql.anilist.co"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    QJsonObject variables;
    variables.insert(QStringLiteral("search"), title);

    QString query_str = QStringLiteral(
        "query ($search: String) { "
        "  Page(page: 1, perPage: 8) { "
        "    media(search: $search, type: MANGA) { "
        "      id "
        "      title { romaji english } "
        "      chapters "
        "      volumes "
        "      status "
        "      coverImage { large medium } "
        "    } "
        "  } "
        "}"
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("query"), query_str);
    payload.insert(QStringLiteral("variables"), variables);

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = network_manager->post(request, body);
    reply->setProperty("sioyek_network_request_type", "anilist_search");

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, {}, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (callback) callback(false, {}, QStringLiteral("Invalid response from AniList"));
            return;
        }

        QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("errors"))) {
            QJsonArray errors = root.value(QStringLiteral("errors")).toArray();
            QString err_msg = errors.isEmpty() ? QStringLiteral("GraphQL Error") : errors.first().toObject().value(QStringLiteral("message")).toString();
            if (callback) callback(false, {}, err_msg);
            return;
        }

        QJsonArray media_list = root.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("Page")).toObject()
                                    .value(QStringLiteral("media")).toArray();

        std::vector<AniListCandidate> candidates;
        for (const QJsonValue& val : media_list) {
            QJsonObject m = val.toObject();
            AniListCandidate c;
            c.id = m.value(QStringLiteral("id")).toInt();
            QJsonObject t = m.value(QStringLiteral("title")).toObject();
            c.title_romaji = t.value(QStringLiteral("romaji")).toString();
            c.title_english = t.value(QStringLiteral("english")).toString();
            c.total_volumes = m.value(QStringLiteral("volumes")).toInt(0);
            c.total_chapters = m.value(QStringLiteral("chapters")).toInt(0);
            c.status = m.value(QStringLiteral("status")).toString();

            QJsonObject cv = m.value(QStringLiteral("coverImage")).toObject();
            c.cover_url = cv.value(QStringLiteral("large")).toString();
            if (c.cover_url.isEmpty()) c.cover_url = cv.value(QStringLiteral("medium")).toString();

            candidates.push_back(c);
        }

        if (callback) callback(true, candidates, QString());
    });
}

void TrackerManager::sync_anilist_progress(int media_id,
                                           int volume,
                                           float chapter,
                                           const QString& token,
                                           std::function<void(bool success, const QString& message)> callback) {
    if (!network_manager) {
        if (callback) callback(false, QStringLiteral("Network manager unavailable"));
        return;
    }
    if (media_id <= 0) {
        if (callback) callback(false, QStringLiteral("No AniList ID configured"));
        return;
    }
    if (token.trimmed().isEmpty()) {
        if (callback) callback(false, QStringLiteral("No anilist_token configured in prefs"));
        return;
    }

    QUrl url(QStringLiteral("https://graphql.anilist.co"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token.trimmed()).toUtf8());

    QJsonObject variables;
    variables.insert(QStringLiteral("id"), media_id);
    if (volume > 0) {
        variables.insert(QStringLiteral("vol"), volume);
    }
    if (chapter > 0.0f) {
        variables.insert(QStringLiteral("ch"), static_cast<int>(chapter));
    }

    QString mutation_str = QStringLiteral(
        "mutation ($id: Int, $vol: Int, $ch: Int) { "
        "  SaveMediaListEntry(mediaId: $id, progressVolumes: $vol, progress: $ch) { "
        "    id "
        "    status "
        "    progress "
        "    progressVolumes "
        "  } "
        "}"
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("query"), mutation_str);
    payload.insert(QStringLiteral("variables"), variables);

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = network_manager->post(request, body);
    reply->setProperty("sioyek_network_request_type", "anilist_sync");

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, volume, chapter]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().contains(QStringLiteral("errors"))) {
            QJsonArray errors = doc.object().value(QStringLiteral("errors")).toArray();
            QString err = errors.isEmpty() ? QStringLiteral("Mutation error") : errors.first().toObject().value(QStringLiteral("message")).toString();
            if (callback) callback(false, err);
            return;
        }

        QString msg = QStringLiteral("AniList synced: ");
        if (volume > 0) msg += QStringLiteral("Vol %1 ").arg(volume);
        if (chapter > 0.0f) msg += QStringLiteral("Ch %1").arg(chapter);
        if (callback) callback(true, msg.trimmed());
    });
}

void TrackerManager::sync_anilist_status(int media_id,
                                         const QString& status,
                                         const QString& token,
                                         std::function<void(bool success, const QString& message)> callback) {
    if (!network_manager) {
        if (callback) callback(false, QStringLiteral("Network manager unavailable"));
        return;
    }
    if (media_id <= 0) {
        if (callback) callback(false, QStringLiteral("No AniList ID configured"));
        return;
    }
    if (token.trimmed().isEmpty()) {
        if (callback) callback(false, QStringLiteral("No anilist_token configured in prefs"));
        return;
    }

    QUrl url(QStringLiteral("https://graphql.anilist.co"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token.trimmed()).toUtf8());

    QJsonObject variables;
    variables.insert(QStringLiteral("id"), media_id);
    variables.insert(QStringLiteral("status"), status);

    QString mutation_str = QStringLiteral(
        "mutation ($id: Int, $status: MediaListStatus) { "
        "  SaveMediaListEntry(mediaId: $id, status: $status) { "
        "    id "
        "    status "
        "  } "
        "}"
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("query"), mutation_str);
    payload.insert(QStringLiteral("variables"), variables);

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = network_manager->post(request, body);
    reply->setProperty("sioyek_network_request_type", "anilist_status");

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, status]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().contains(QStringLiteral("errors"))) {
            QJsonArray errors = doc.object().value(QStringLiteral("errors")).toArray();
            QString err_msg = errors.isEmpty() ? QStringLiteral("GraphQL Error") : errors.first().toObject().value(QStringLiteral("message")).toString();
            if (callback) callback(false, err_msg);
            return;
        }

        if (callback) callback(true, QStringLiteral("AniList status updated: %1").arg(status));
    });
}

void TrackerManager::sync_floppy_progress(const QString& series_title,
                                          int volume,
                                          float chapter,
                                          const QString& floppy_url,
                                          const QString& token,
                                          std::function<void(bool success, const QString& message)> callback) {
    if (!network_manager) {
        if (callback) callback(false, QStringLiteral("Network manager unavailable"));
        return;
    }
    if (floppy_url.trimmed().isEmpty()) {
        if (callback) callback(false, QStringLiteral("floppy_url not set"));
        return;
    }

    QString base = floppy_url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }

    QUrl url(QStringLiteral("%1/api/v1/scrobble").arg(base));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.trimmed().isEmpty()) {
        QString raw_token = token.trimmed();
        if (raw_token.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive)) {
            raw_token = raw_token.mid(7).trimmed();
        } else if (raw_token.startsWith(QLatin1String("Token "), Qt::CaseInsensitive)) {
            raw_token = raw_token.mid(6).trimmed();
        }
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(raw_token).toUtf8());
        request.setRawHeader("X-API-Key", raw_token.toUtf8());
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("title"), series_title);
    payload.insert(QStringLiteral("type"), QStringLiteral("manga"));
    if (volume > 0) payload.insert(QStringLiteral("volume"), volume);
    if (chapter > 0.0f) payload.insert(QStringLiteral("chapter"), chapter);
    payload.insert(QStringLiteral("action"), QStringLiteral("stop"));

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = network_manager->post(request, body);
    reply->setProperty("sioyek_network_request_type", "floppy_sync");

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, series_title]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, QStringLiteral("Floppy error: %1").arg(reply->errorString()));
            return;
        }
        if (callback) callback(true, QStringLiteral("Floppy synced: %1").arg(series_title));
    });
}
