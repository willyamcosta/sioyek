#include <qfile.h>

#include "checksum.h"

std::string compute_checksum(const QString& file_name, QCryptographicHash::Algorithm hash_algorithm)
{
    QFile infile(file_name);
    qint64 file_size = infile.size();
    const qint64 buffer_size = 1048576; // 1 MB buffer

    if (infile.open(QIODevice::ReadOnly))
    {
        std::vector<char> buffer(buffer_size);
        int bytes_read;
        int read_size = qMin(file_size, buffer_size);

        QCryptographicHash hash(hash_algorithm);
        while (read_size > 0 && (bytes_read = infile.read(buffer.data(), read_size)) > 0)
        {
            file_size -= bytes_read;
            hash.addData(buffer.data(), bytes_read);
            read_size = qMin(file_size, buffer_size);
        }

        infile.close();
        return QString(hash.result().toHex()).toStdString();
    }
    return "";
}

CachedChecksummer::CachedChecksummer(const std::vector<std::pair<std::wstring, std::wstring>>* loaded_checksums) {
    if (loaded_checksums) {
        for (const auto& [path, checksum_] : *loaded_checksums) {
            std::string checksum = QString::fromStdWString(checksum_).toStdString();
            cached_checksums[path] = checksum;
            cached_paths[checksum].push_back(path);
        }
    }
}

std::optional<std::string> CachedChecksummer::get_checksum_fast(std::wstring file_path) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cached_checksums.find(file_path);
    if (it != cached_checksums.end()) {
        return it->second;
    }
    return {};
}

std::string CachedChecksummer::get_checksum(std::wstring file_path) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cached_checksums.find(file_path);
        if (it != cached_checksums.end()) {
            return it->second;
        }
    }

    std::string checksum = compute_checksum(QString::fromStdWString(file_path), QCryptographicHash::Md5);

    {
        std::lock_guard<std::mutex> lock(mutex);
        cached_checksums[file_path] = checksum;
        cached_paths[checksum].push_back(file_path);
        return checksum;
    }
}

std::optional<std::wstring> CachedChecksummer::get_path(std::string checksum) {
    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cached_paths.find(checksum);
        if (it != cached_paths.end()) {
            paths = it->second;
        }
    }

    for (const auto& path_string : paths) {
        if (QFile::exists(QString::fromStdWString(path_string))) {
            return path_string;
        }
    }
    return {};
}

int CachedChecksummer::num_docs_with_checksum(std::string checksum) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cached_paths.find(checksum);
    if (it != cached_paths.end()) {
        return static_cast<int>(it->second.size());
    }
    return 0;
}