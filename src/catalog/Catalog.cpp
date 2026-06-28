#include "Catalog.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace mylr {

Catalog::Catalog(QObject* parent) : QObject(parent) {}

Catalog::~Catalog() { close(); }

bool Catalog::open(const QString& dbPath) {
    close();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("mylr_catalog"));
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) return false;
    return ensureSchema();
}

void Catalog::close() {
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("mylr_catalog"));
    }
}

bool Catalog::ensureSchema() {
    // The QSQLITE driver executes only the FIRST statement of a multi-statement
    // string, so each CREATE TABLE must be issued separately.
    static const char* const statements[] = {
        "CREATE TABLE IF NOT EXISTS images ("
        "id TEXT PRIMARY KEY, file_path TEXT UNIQUE, file_name TEXT,"
        "rating INTEGER DEFAULT 0, color_label TEXT,"
        "imported_at TEXT, modified_at TEXT,"
        "width INTEGER, height INTEGER, camera TEXT, iso INTEGER"
        ");",
        "CREATE TABLE IF NOT EXISTS folders ("
        "id TEXT PRIMARY KEY, path TEXT UNIQUE, added_at TEXT"
        ");",
        "CREATE TABLE IF NOT EXISTS collections ("
        "id TEXT PRIMARY KEY, name TEXT UNIQUE, created_at TEXT"
        ");",
        "CREATE TABLE IF NOT EXISTS collection_images ("
        "collection_id TEXT, image_id TEXT,"
        "PRIMARY KEY(collection_id, image_id)"
        ");",
        "CREATE TABLE IF NOT EXISTS virtual_copies ("
        "id TEXT PRIMARY KEY, master_id TEXT, name TEXT, created_at TEXT,"
        "FOREIGN KEY(master_id) REFERENCES images(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS match_profiles ("
        "id TEXT PRIMARY KEY, image_id TEXT, profile_json TEXT,"
        "FOREIGN KEY(image_id) REFERENCES images(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS export_jobs ("
        "id TEXT PRIMARY KEY, created_at TEXT, settings_json TEXT, status TEXT"
        ");",
    };
    for (const char* sql : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(QString::fromLatin1(sql)))
            return false;
    }
    return true;
}

QString Catalog::importImage(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return {};

    CatalogImage img;
    img.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    img.filePath = fi.absoluteFilePath();
    img.fileName = fi.fileName();
    img.importedAt = QDateTime::currentDateTime();
    img.modifiedAt = img.importedAt;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO images (id, file_path, file_name, rating, color_label,"
        "imported_at, modified_at, width, height, camera, iso)"
        " VALUES (?, ?, ?, 0, '', ?, ?, 0, 0, '', 0)"));
    q.addBindValue(img.id);
    q.addBindValue(img.filePath);
    q.addBindValue(img.fileName);
    q.addBindValue(img.importedAt.toString(Qt::ISODate));
    q.addBindValue(img.modifiedAt.toString(Qt::ISODate));
    if (!q.exec()) return {};
    emit catalogChanged();
    return img.id;
}

bool Catalog::removeImage(const QString& id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM images WHERE id = ?"));
    q.addBindValue(id);
    const bool ok = q.exec();
    if (ok) emit catalogChanged();
    return ok;
}

QVector<CatalogImage> Catalog::allImages() const {
    QVector<CatalogImage> result;
    QSqlQuery q(QStringLiteral("SELECT * FROM images ORDER BY imported_at DESC"), m_db);
    while (q.next()) {
        CatalogImage img;
        img.id = q.value(QStringLiteral("id")).toString();
        img.filePath = q.value(QStringLiteral("file_path")).toString();
        img.fileName = q.value(QStringLiteral("file_name")).toString();
        img.rating = q.value(QStringLiteral("rating")).toInt();
        img.colorLabel = q.value(QStringLiteral("color_label")).toString();
        img.importedAt = QDateTime::fromString(q.value(QStringLiteral("imported_at")).toString(), Qt::ISODate);
        img.width = q.value(QStringLiteral("width")).toInt();
        img.height = q.value(QStringLiteral("height")).toInt();
        img.camera = q.value(QStringLiteral("camera")).toString();
        img.iso = q.value(QStringLiteral("iso")).toInt();
        result.append(img);
    }
    return result;
}

QVector<CatalogImage> Catalog::imagesInFolder(const QString& folder) const {
    QVector<CatalogImage> all = allImages();
    QVector<CatalogImage> filtered;
    const QString norm = QDir::fromNativeSeparators(folder);
    for (const auto& img : all) {
        if (QFileInfo(img.filePath).absolutePath().startsWith(norm))
            filtered.append(img);
    }
    return filtered;
}

CatalogImage Catalog::imageById(const QString& id) const {
    for (const auto& img : allImages()) {
        if (img.id == id) return img;
    }
    return {};
}

bool Catalog::updateRating(const QString& path, int rating) {
    const QString id = idForPath(path);
    if (id.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE images SET rating = ? WHERE id = ?"));
    q.addBindValue(rating);
    q.addBindValue(id);
    return q.exec();
}

bool Catalog::updateColorLabel(const QString& path, const QString& label) {
    const QString id = idForPath(path);
    if (id.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE images SET color_label = ? WHERE id = ?"));
    q.addBindValue(label);
    q.addBindValue(id);
    return q.exec();
}

bool Catalog::updateMetadata(const QString& path, int width, int height,
                             const QString& camera, int iso) {
    const QString id = idForPath(path);
    if (id.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE images SET width = ?, height = ?, camera = ?, iso = ? WHERE id = ?"));
    q.addBindValue(width);
    q.addBindValue(height);
    q.addBindValue(camera);
    q.addBindValue(iso);
    q.addBindValue(id);
    return q.exec();
}

QString Catalog::idForPath(const QString& path) const {
    if (!m_db.isOpen()) return {};
    const QString absPath = QFileInfo(path).absoluteFilePath();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM images WHERE file_path = ?"));
    q.addBindValue(absPath);
    if (q.exec() && q.next())
        return q.value(0).toString();
    // Auto-register so callers that pass a path always succeed.
    return const_cast<Catalog*>(this)->importImage(absPath);
}

QString Catalog::addFolder(const QString& path) {
    if (!m_db.isOpen()) return {};
    const QString abs = QDir(path).absolutePath();
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral("SELECT id FROM folders WHERE path = ?"));
    find.addBindValue(abs);
    if (find.exec() && find.next())
        return find.value(0).toString();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO folders (id, path, added_at) VALUES (?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(abs);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) return {};
    emit catalogChanged();
    return id;
}

QStringList Catalog::folders() const {
    QStringList result;
    QSqlQuery q(QStringLiteral("SELECT path FROM folders ORDER BY path"), m_db);
    while (q.next()) result << q.value(0).toString();
    return result;
}

QString Catalog::createCollection(const QString& name) {
    if (!m_db.isOpen()) return {};
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral("SELECT id FROM collections WHERE name = ?"));
    find.addBindValue(name);
    if (find.exec() && find.next())
        return find.value(0).toString();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO collections (id, name, created_at) VALUES (?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(name);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) return {};
    emit catalogChanged();
    return id;
}

QVector<CollectionInfo> Catalog::collections() const {
    QVector<CollectionInfo> result;
    QSqlQuery q(QStringLiteral("SELECT id, name FROM collections ORDER BY name"), m_db);
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toString()});
    return result;
}

bool Catalog::addImageToCollection(const QString& collectionId, const QString& imagePath) {
    const QString imageId = idForPath(imagePath);
    if (collectionId.isEmpty() || imageId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO collection_images (collection_id, image_id) VALUES (?, ?)"));
    q.addBindValue(collectionId);
    q.addBindValue(imageId);
    const bool ok = q.exec();
    if (ok) emit catalogChanged();
    return ok;
}

QStringList Catalog::imagesInCollection(const QString& collectionId) const {
    QStringList result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT i.file_path FROM images i JOIN collection_images c ON i.id = c.image_id "
        "WHERE c.collection_id = ? ORDER BY i.file_name"));
    q.addBindValue(collectionId);
    if (q.exec())
        while (q.next()) result << q.value(0).toString();
    return result;
}

bool Catalog::saveMatchProfile(const QString& imagePath, const QString& profileJson) {
    const QString imageId = idForPath(imagePath);
    if (imageId.isEmpty()) return false;
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM match_profiles WHERE image_id = ?"));
    del.addBindValue(imageId);
    del.exec();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO match_profiles (id, image_id, profile_json) VALUES (?, ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(imageId);
    q.addBindValue(profileJson);
    return q.exec();
}

QString Catalog::loadMatchProfile(const QString& imagePath) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT p.profile_json FROM match_profiles p JOIN images i ON i.id = p.image_id "
        "WHERE i.file_path = ?"));
    q.addBindValue(QFileInfo(imagePath).absoluteFilePath());
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool Catalog::moveImageFile(const QString& oldPath, const QString& newDir) {
    if (!m_db.isOpen()) return false;
    const QFileInfo fi(oldPath);
    if (!fi.exists()) return false;
    QDir().mkpath(newDir);
    const QString newPath = QDir(newDir).absoluteFilePath(fi.fileName());
    if (QFileInfo(newPath).absoluteFilePath() == fi.absoluteFilePath()) return true;
    if (QFile::exists(newPath)) return false;
    if (!QFile::rename(fi.absoluteFilePath(), newPath)) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE images SET file_path = ? WHERE file_path = ?"));
    q.addBindValue(newPath);
    q.addBindValue(fi.absoluteFilePath());
    q.exec();
    emit catalogChanged();
    return true;
}

QString Catalog::createVirtualCopy(const QString& masterPath, const QString& name) {
    if (!m_db.isOpen()) return {};
    const QString absPath = QFileInfo(masterPath).absoluteFilePath();

    QString masterId;
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral("SELECT id FROM images WHERE file_path = ?"));
    find.addBindValue(absPath);
    if (find.exec() && find.next())
        masterId = find.value(0).toString();
    if (masterId.isEmpty())
        masterId = importImage(masterPath);
    if (masterId.isEmpty()) return {};

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO virtual_copies (id, master_id, name, created_at) VALUES (?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(masterId);
    q.addBindValue(name);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) return {};
    emit catalogChanged();
    return id;
}

} // namespace mylr