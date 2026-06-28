#pragma once

#include "../ai/MatchEngine.hpp"
#include "../catalog/Catalog.hpp"
#include "../core/EditGraph.hpp"
#include "../core/ImageCache.hpp"
#include "../core/PresetManager.hpp"
#include "../core/SidecarIO.hpp"
#include "../core/ThumbnailCache.hpp"
#include "../export/ExportEngine.hpp"
#include "../raw/RawDecoder.hpp"
#include "../render/DevelopPipeline.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace mylr {

struct BatchMatchItem {
    int index = -1;
    float confidence = 0.f;
    QImage thumbnail;
};

class DocumentController : public QObject {
    Q_OBJECT
public:
    explicit DocumentController(QObject* parent = nullptr);

    EditGraph* editGraph() { return &m_editGraph; }
    MatchEngine* matchEngine() { return &m_matchEngine; }
    PresetManager* presetManager() { return &m_presetManager; }
    Catalog* catalog() { return &m_catalog; }

    bool openCatalog(const QString& dbPath);
    QStringList importFolder(const QString& folder);
    // Show an explicit list of images (e.g. a collection) without re-importing.
    void setImagePaths(const QStringList& paths);
    bool loadImage(int index);
    int currentIndex() const { return m_currentIndex; }
    const QStringList& imagePaths() const { return m_paths; }

    QImage currentPreview() const { return m_rendered; }
    // True when the most recent previewUpdated came from the fast interactive
    // (drag) path, so listeners can defer expensive work like histogram recompute.
    bool lastPreviewInteractive() const { return m_previewInteractive; }
    // Baseline ("before") render, computed lazily on first request (it is only
    // needed for the before/after view and the AI compare panel).
    QImage beforePreview();
    RawImage currentRaw() const { return m_raw; }
    QImage thumbnailForPath(const QString& path) const;
    // Lazily classifies the current image only when first requested (e.g. when
    // the AI module is shown). Avoids paying the cost on every plain load.
    QString currentSceneType();
    float lastMatchConfidence() const { return m_lastConfidence; }

    void refreshPreview();              // schedule full-resolution async render
    void requestInteractivePreview();   // fast low-res render, debounced full render
    // Cap the async preview render to roughly the on-screen viewport size; full
    // native resolution is only used on Export.
    void setPreviewMaxEdge(int edge) { m_previewMaxEdge = qMax(256, edge); }
    void saveCurrentSidecar();
    MatchProfile saveReferenceProfile();
    // Set the given image as the AI reference photo. Loads it if needed, renders
    // its current settings synchronously (avoiding the async race where capture
    // would otherwise use the previous image's stale render), captures the
    // reference profile, and emits referencePhotoSet. Returns true on success.
    bool setAsReferencePhoto(int index);
    // Rendered reference look (set when a reference is captured); used by the
    // AI compare view and Reference Photo Mode.
    QImage referenceImage() const { return m_referenceImage; }
    bool applyMatchToCurrent();
    int applyMatchToSelected(const QVector<int>& indices);
    // Background "Apply Match to All": matches/saves each image on the thread
    // pool and streams thumbnails back via matchThumbnailReady, keeping the UI
    // responsive. matchCompleted fires when the whole queue is done.
    void applyMatchToAllAsync(const QVector<int>& indices);
    bool isBatchMatchRunning() const;

    // AI auto-adjust (heuristic). Returns true if applied.
    bool applyAutoTone();
    bool applyAutoExposure();
    bool applyAutoWhiteBalance();
    // Adjust the current image's exposure so its mean lightness matches the
    // captured AI reference (Lightroom "Match Total Exposures").
    bool matchTotalExposures();

    bool exportCurrent(const ExportSettings& settings, const QString& outputPath);
    bool exportBatch(const ExportSettings& settings);

signals:
    void previewUpdated();
    void imageLoaded(int index);
    void errorOccurred(const QString& message);
    void matchCompleted(int count, float avgConfidence);
    void matchProgress(int done, int total);
    void matchThumbnailReady(int index, const QImage& thumbnail);
    void referencePhotoSet(int index, const QImage& referenceImage);

public slots:
    void applyPreset(const QString& presetId);

private:
    void startAsyncRender(quint64 generation);
    void computeSceneType();
    void renderInteractiveNow();

    EditGraph m_editGraph;
    MatchEngine m_matchEngine;
    PresetManager m_presetManager;
    Catalog m_catalog;
    RawDecoder m_decoder;
    DevelopPipeline m_pipeline;
    ImageCache m_imageCache;
    ThumbnailCache m_thumbCache;

    QStringList m_paths;
    int m_currentIndex = -1;
    RawImage m_raw;
    QImage m_interactiveSource;  // downscaled preview for fast feedback while dragging
    QImage m_rendered;
    QImage m_beforeRender;
    bool m_beforeComputed = false;
    MatchProfile m_matchProfile;
    QString m_sceneType;
    bool m_sceneComputed = false;
    float m_lastConfidence = -1.f;

    QImage m_referenceImage;

    QFutureWatcher<QImage> m_renderWatcher;
    quint64 m_renderGeneration = 0;
    QTimer m_debounceTimer;
    int m_previewMaxEdge = 1920;

    // Throttle for the interactive (drag) preview: render at most once per
    // interval with a trailing render so fast dragging stays smooth.
    QTimer m_interactiveThrottle;
    bool m_interactiveDirty = false;
    bool m_previewInteractive = false;

    QFutureWatcher<BatchMatchItem> m_batchWatcher;
    QVector<int> m_batchIndices;
    float m_batchConfSum = 0.f;
    int m_batchCount = 0;
    int m_batchSavedIndex = -1;
};

} // namespace mylr
