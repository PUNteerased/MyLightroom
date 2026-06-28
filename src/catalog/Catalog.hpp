#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSqlDatabase>
#include <QDateTime>

namespace mylr {

struct CatalogImage {
    QString id;
    QString filePath;
    QString fileName;
    int rating = 0;
    QString colorLabel;
    QDateTime importedAt;
    QDateTime modifiedAt;
    int width = 0;
    int height = 0;
    QString camera;
    int iso = 0;
};

struct CollectionInfo {
    QString id;
    QString name;
};

class Catalog : public QObject {
    Q_OBJECT
public:
    explicit Catalog(QObject* parent = nullptr);
    ~Catalog();

    bool open(const QString& dbPath);
    void close();

    QString importImage(const QString& path);
    bool removeImage(const QString& id);
    QVector<CatalogImage> allImages() const;
    QVector<CatalogImage> imagesInFolder(const QString& folder) const;
    CatalogImage imageById(const QString& id) const;
    // Rating/label are keyed by the image file path (matches the UI callers).
    bool updateRating(const QString& path, int rating);
    bool updateColorLabel(const QString& path, const QString& label);
    // Populate technical metadata once an image has actually been decoded.
    bool updateMetadata(const QString& path, int width, int height, const QString& camera, int iso);

    // Creates a virtual copy row referencing the master image (by file path).
    QString createVirtualCopy(const QString& masterPath,
                              const QString& name = QStringLiteral("Copy"));

    // Folders.
    QString addFolder(const QString& path);
    QStringList folders() const;

    // Collections.
    QString createCollection(const QString& name);
    QVector<CollectionInfo> collections() const;
    bool addImageToCollection(const QString& collectionId, const QString& imagePath);
    QStringList imagesInCollection(const QString& collectionId) const;

    // Match-profile persistence (JSON blob keyed by image path).
    bool saveMatchProfile(const QString& imagePath, const QString& profileJson);
    QString loadMatchProfile(const QString& imagePath) const;

    // Move a file on disk and update its stored path (avoids "missing files").
    bool moveImageFile(const QString& oldPath, const QString& newDir);

    QString cachePath() const { return m_cachePath; }
    void setCachePath(const QString& path) { m_cachePath = path; }

signals:
    void catalogChanged();

private:
    bool ensureSchema();
    QString idForPath(const QString& path) const;  // ensures a row exists
    QSqlDatabase m_db;
    QString m_cachePath;
};

} // namespace mylr
