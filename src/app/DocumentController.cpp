#include "DocumentController.hpp"
#include "../ai/AutoAdjust.hpp"
#include "../ai/FeatureExtractor.hpp"
#include "../ai/SceneClassifier.hpp"
#include "../core/LinearRgb64.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtConcurrent>
#include <array>
#include <cmath>

namespace mylr {

DocumentController::DocumentController(QObject* parent) : QObject(parent) {
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString cache = QDir(cacheRoot).filePath(QStringLiteral("cache_v101_linear_fix"));
    QDir().mkpath(cache);
    m_catalog.setCachePath(cache);
    m_thumbCache.setDirectory(QDir(cache).filePath(QStringLiteral("thumbnails")));
    m_imageCache.clear();
    m_presetManager.loadDirectory(QStringLiteral("presets"));
    QDir().mkpath(QStringLiteral("presets/luts"));

    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(90);
    connect(&m_debounceTimer, &QTimer::timeout, this, [this]() { refreshPreview(); });

    // Leading-edge throttle for interactive drag previews (~24ms ≈ 40fps).
    m_interactiveThrottle.setSingleShot(true);
    m_interactiveThrottle.setInterval(24);
    connect(&m_interactiveThrottle, &QTimer::timeout, this, [this]() {
        if (m_interactiveDirty) {
            renderInteractiveNow();
            m_interactiveThrottle.start();  // keep the trailing window open
        }
    });

    connect(&m_renderWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        if (m_renderWatcher.future().isValid()) {
            const QImage result = m_renderWatcher.result();
            if (!result.isNull()) {
                m_rendered = result;
                m_previewInteractive = false;  // full-resolution result
                emit previewUpdated();
            }
        }
    });

    connect(&m_batchWatcher, &QFutureWatcher<BatchMatchItem>::resultReadyAt, this, [this](int i) {
        const BatchMatchItem item = m_batchWatcher.resultAt(i);
        if (item.index < 0) return;
        m_batchConfSum += item.confidence;
        ++m_batchCount;
        if (!item.thumbnail.isNull())
            emit matchThumbnailReady(item.index, item.thumbnail);
        emit matchProgress(m_batchCount, m_batchIndices.size());
    });
    connect(&m_batchWatcher, &QFutureWatcher<BatchMatchItem>::finished, this, [this]() {
        emit matchCompleted(m_batchCount,
                            m_batchCount > 0 ? m_batchConfSum / m_batchCount : 0.f);
        m_batchSavedIndex = -1;
        // Reload whichever image is currently selected (which may differ from the
        // one active when the batch started) so it reflects its freshly-saved
        // sidecar, instead of snapping the user back to the pre-batch photo.
        const int cur = m_currentIndex;
        if (cur >= 0) {
            m_currentIndex = -1;
            loadImage(cur);
        }
    });
}

bool DocumentController::isBatchMatchRunning() const {
    return m_batchWatcher.isRunning();
}

void DocumentController::applyMatchToAllAsync(const QVector<int>& indices) {
    if (!m_matchEngine.hasReference()) {
        emit errorOccurred(QStringLiteral("Capture a reference look first"));
        return;
    }
    if (m_batchWatcher.isRunning()) return;

    m_batchIndices = indices;
    m_batchConfSum = 0.f;
    m_batchCount = 0;
    m_batchSavedIndex = m_currentIndex;

    const QStringList paths = m_paths;
    MatchEngine* engine = &m_matchEngine;
    const RawDecoder* decoder = &m_decoder;
    auto work = [paths, engine, decoder](int idx) -> BatchMatchItem {
        BatchMatchItem item;
        item.index = idx;
        if (idx < 0 || idx >= paths.size()) return item;
        const QString path = paths[idx];
        RawImage raw = decoder->decode(path, 1024);
        if (!raw.valid) return item;
        const MatchResult r = engine->matchImage(raw.preview, raw.metadata);
        SidecarIO::save(path, r.settings, nullptr);
        DevelopPipeline pipeline;
        item.thumbnail = pipeline.renderLinear(raw.linearRgb, r.settings,
                                               raw.metadata.wbCoeffs, raw.metadata.rgbCam, 256,
                                               raw.metadata.isCameraLinear);
        item.confidence = r.confidence;
        return item;
    };
    m_batchWatcher.setFuture(QtConcurrent::mapped(m_batchIndices, work));
}

bool DocumentController::openCatalog(const QString& dbPath) {
    return m_catalog.open(dbPath);
}

QStringList DocumentController::importFolder(const QString& folder) {
    QDir dir(folder);
    QStringList filters;
    for (const auto& ext : RawDecoder::supportedExtensions())
        filters << QStringLiteral("*.") + ext;
    filters << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg") << QStringLiteral("*.png");

    m_catalog.addFolder(folder);
    m_paths.clear();
    for (const auto& fi : dir.entryInfoList(filters, QDir::Files)) {
        m_paths.append(fi.absoluteFilePath());
        m_catalog.importImage(fi.absoluteFilePath());
    }
    if (!m_paths.isEmpty())
        loadImage(0);
    return m_paths;
}

void DocumentController::setImagePaths(const QStringList& paths) {
    m_paths = paths;
    m_currentIndex = -1;
    if (!m_paths.isEmpty())
        loadImage(0);
}

bool DocumentController::loadImage(int index) {
    if (index < 0 || index >= m_paths.size()) return false;
    m_currentIndex = index;
    const QString path = m_paths[index];

    // Never reuse stale canvas or cached decode while validating the color pipeline.
    m_rendered = QImage();
    m_beforeRender = QImage();
    m_beforeComputed = false;

    const QString cacheKey = ImageCache::cacheKey(path);
    m_raw = m_decoder.decode(path, 2048);
    if (m_raw.valid)
        m_imageCache.put(cacheKey, m_raw);
    if (!m_raw.valid) {
        emit errorOccurred(QStringLiteral("Failed to decode: %1").arg(path));
        return false;
    }

    // Downscaled LINEAR source (RGBX64) for fast feedback while dragging sliders.
    m_interactiveSource = scaleLinearRgb64(m_raw.linearRgb, 1024);

    // Populate technical metadata now that the file is decoded (cheap, per-view).
    m_catalog.updateMetadata(path, m_raw.metadata.width, m_raw.metadata.height,
                             m_raw.metadata.cameraModel, m_raw.metadata.iso);

    DevelopSettings settings = DevelopSettings::defaults();
    SidecarIO::load(path, settings, &m_matchProfile);
    m_editGraph.setSettings(settings, QStringLiteral("Load"));

    if (m_matchProfile.referenceImageId.isEmpty() == false)
        m_matchEngine.setReferenceProfile(m_matchProfile);

    // "Before" baseline is deferred until actually requested (before/after view
    // or AI compare); a plain load no longer pays for a second full render.
    m_beforeRender = QImage();
    m_beforeComputed = false;

    // Scene classification is deferred until actually needed (AI module / match)
    // so a plain load/import never pays the AI cost.
    m_sceneType.clear();
    m_sceneComputed = false;

    // Synchronous unified-pipeline render for the canvas (never raw.preview / embedded thumb).
    m_rendered = m_pipeline.renderLinear(m_raw.linearRgb, m_editGraph.current(),
                                         m_raw.metadata.wbCoeffs, m_raw.metadata.rgbCam,
                                         m_previewMaxEdge, m_raw.metadata.isCameraLinear);
    m_previewInteractive = false;
    emit previewUpdated();

    refreshPreview();
    emit imageLoaded(index);
    return true;
}

QImage DocumentController::beforePreview() {
    if (!m_beforeComputed && m_raw.valid) {
        m_beforeRender = m_pipeline.renderLinear(m_raw.linearRgb, DevelopSettings::defaults(),
                                                 m_raw.metadata.wbCoeffs, m_raw.metadata.rgbCam, 0,
                                                 m_raw.metadata.isCameraLinear);
        m_beforeComputed = true;
    }
    return m_beforeRender;
}

QString DocumentController::currentSceneType() {
    if (!m_sceneComputed)
        computeSceneType();
    return m_sceneType;
}

void DocumentController::computeSceneType() {
    m_sceneComputed = true;
    if (!m_raw.valid) {
        m_sceneType.clear();
        return;
    }
    FeatureExtractor fx;
    const SceneFeatures sf = fx.extract(m_raw.preview, m_raw.metadata);
    m_sceneType = SceneClassifier::classify(sf.fingerprint);
}

void DocumentController::startAsyncRender(quint64) {
    if (!m_raw.valid) return;
    const DevelopSettings settings = m_editGraph.current();
    const QImage source = m_raw.linearRgb;
    const int maxEdge = m_previewMaxEdge;
    const std::array<float, 4> wb{m_raw.metadata.wbCoeffs[0], m_raw.metadata.wbCoeffs[1],
                                  m_raw.metadata.wbCoeffs[2], m_raw.metadata.wbCoeffs[3]};
    const std::array<float, 9> rgbCam{m_raw.metadata.rgbCam[0], m_raw.metadata.rgbCam[1],
                                      m_raw.metadata.rgbCam[2], m_raw.metadata.rgbCam[3],
                                      m_raw.metadata.rgbCam[4], m_raw.metadata.rgbCam[5],
                                      m_raw.metadata.rgbCam[6], m_raw.metadata.rgbCam[7],
                                      m_raw.metadata.rgbCam[8]};
    const bool isCameraLinear = m_raw.metadata.isCameraLinear;
    QFuture<QImage> future = QtConcurrent::run([source, settings, maxEdge, wb, rgbCam, isCameraLinear]() {
        DevelopPipeline pipeline;
        return pipeline.renderLinear(source, settings, wb.data(), rgbCam.data(), maxEdge,
                                     isCameraLinear);
    });
    m_renderWatcher.setFuture(future);
}

void DocumentController::refreshPreview() {
    if (!m_raw.valid) return;
    ++m_renderGeneration;
    startAsyncRender(m_renderGeneration);
}

void DocumentController::requestInteractivePreview() {
    if (!m_raw.valid) return;
    m_interactiveDirty = true;
    // Leading edge: render immediately if the throttle window is open, otherwise
    // the trailing timer will pick up the latest state.
    if (!m_interactiveThrottle.isActive()) {
        renderInteractiveNow();
        m_interactiveThrottle.start();
    }
}

void DocumentController::renderInteractiveNow() {
    if (!m_raw.valid) return;
    m_interactiveDirty = false;
    // Fast low-resolution render for responsive slider dragging...
    m_rendered = m_pipeline.renderLinear(m_interactiveSource, m_editGraph.current(),
                                         m_raw.metadata.wbCoeffs, m_raw.metadata.rgbCam, 0,
                                         m_raw.metadata.isCameraLinear);
    m_previewInteractive = true;
    emit previewUpdated();
    // ...then a full-resolution render once the user pauses.
    m_debounceTimer.start();
}

void DocumentController::saveCurrentSidecar() {
    if (m_currentIndex < 0) return;
    const QString path = m_paths[m_currentIndex];
    const MatchProfile* mp = m_matchProfile.referenceImageId.isEmpty() ? nullptr : &m_matchProfile;
    SidecarIO::save(path, m_editGraph.current(), mp);
}

MatchProfile DocumentController::saveReferenceProfile() {
    if (!m_raw.valid || m_currentIndex < 0) return {};
    const QString id = m_paths[m_currentIndex];
    FeatureExtractor fx;
    const SceneFeatures sf = fx.extract(m_rendered, m_raw.metadata);
    m_matchProfile = m_matchEngine.captureReference(m_rendered, m_raw.metadata,
                                                    m_editGraph.current(), id);
    m_referenceImage = m_rendered;
    saveCurrentSidecar();
    // Persist the match profile in the catalog as well (JSON blob keyed by path).
    const QJsonObject obj = SidecarIO::matchProfileToJson(m_matchProfile);
    m_catalog.saveMatchProfile(
        id, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    return m_matchProfile;
}

bool DocumentController::setAsReferencePhoto(int index) {
    if (index < 0 || index >= m_paths.size()) return false;
    if (index != m_currentIndex && !loadImage(index)) return false;
    if (!m_raw.valid) return false;
    // loadImage schedules an async render, so m_rendered may still hold the
    // previous image. Render the current settings synchronously here so the
    // captured reference reflects exactly this image.
    m_rendered = m_pipeline.renderLinear(m_raw.linearRgb, m_editGraph.current(),
                                         m_raw.metadata.wbCoeffs, m_raw.metadata.rgbCam, 0,
                                         m_raw.metadata.isCameraLinear);
    const MatchProfile profile = saveReferenceProfile();
    if (profile.referenceImageId.isEmpty()) return false;
    emit referencePhotoSet(index, m_referenceImage);
    return true;
}

bool DocumentController::applyMatchToCurrent() {
    if (!m_raw.valid) return false;
    const MatchResult r = m_matchEngine.matchImage(m_raw.preview, m_raw.metadata);
    if (!r.warning.isEmpty())
        emit errorOccurred(r.warning);
    m_lastConfidence = r.confidence;
    m_editGraph.setSettings(r.settings, QStringLiteral("AI Match"));
    refreshPreview();
    saveCurrentSidecar();
    return true;
}

bool DocumentController::applyAutoTone() {
    if (!m_raw.valid) return false;
    auto s = m_editGraph.current();
    s.basic = AutoAdjust::autoTone(m_raw.preview, s.basic);
    m_editGraph.setSettings(s, QStringLiteral("Auto Tone"));
    refreshPreview();
    saveCurrentSidecar();
    return true;
}

bool DocumentController::applyAutoExposure() {
    if (!m_raw.valid) return false;
    auto s = m_editGraph.current();
    s.basic = AutoAdjust::autoExposure(m_raw.preview, s.basic);
    m_editGraph.setSettings(s, QStringLiteral("Auto Exposure"));
    refreshPreview();
    saveCurrentSidecar();
    return true;
}

bool DocumentController::applyAutoWhiteBalance() {
    if (!m_raw.valid) return false;
    auto s = m_editGraph.current();
    s.basic = AutoAdjust::autoWhiteBalance(m_raw.preview, s.basic);
    m_editGraph.setSettings(s, QStringLiteral("Auto White Balance"));
    refreshPreview();
    saveCurrentSidecar();
    return true;
}

bool DocumentController::matchTotalExposures() {
    if (!m_raw.valid || !m_matchEngine.hasReference()) {
        emit errorOccurred(QStringLiteral("Capture a reference look first"));
        return false;
    }
    const float targetL = m_matchEngine.referenceProfile().fingerprint.labLMean;  // 0..100
    if (targetL <= 0.f) return false;

    const QImage img = m_rendered.isNull() ? m_raw.preview : m_rendered;
    const QImage rgb = (img.format() == QImage::Format_RGB32 ||
                        img.format() == QImage::Format_ARGB32)
                           ? img
                           : img.convertToFormat(QImage::Format_RGB32);
    double sum = 0.0;
    qint64 n = 0;
    for (int y = 0; y < rgb.height(); y += 2) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += 2) {
            sum += 0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) + 0.0722 * qBlue(line[x]);
            ++n;
        }
    }
    if (n == 0) return false;
    const float curL = static_cast<float>(sum / n) / 255.f * 100.f;  // approx Lab L
    if (curL <= 1.f) return false;

    const float ev = qBound(-3.f, std::log2(targetL / curL), 3.f);
    auto s = m_editGraph.current();
    s.basic.exposure = qBound(-5.f, s.basic.exposure + ev, 5.f);
    m_editGraph.setSettings(s, QStringLiteral("Match Total Exposures"));
    refreshPreview();
    saveCurrentSidecar();
    return true;
}

int DocumentController::applyMatchToSelected(const QVector<int>& indices) {
    float confSum = 0.f;
    int count = 0;
    const int saved = m_currentIndex;

    for (int idx : indices) {
        if (!loadImage(idx)) continue;
        const MatchResult r = m_matchEngine.matchImage(m_raw.preview, m_raw.metadata);
        m_editGraph.setSettings(r.settings, QStringLiteral("AI Batch Match"));
        refreshPreview();
        saveCurrentSidecar();
        confSum += r.confidence;
        ++count;
    }

    if (saved >= 0) loadImage(saved);
    emit matchCompleted(count, count > 0 ? confSum / count : 0.f);
    return count;
}

void DocumentController::applyPreset(const QString& presetId) {
    auto s = m_editGraph.current();
    if (m_presetManager.applyPreset(presetId, s)) {
        m_editGraph.setSettings(s, QStringLiteral("Preset"));
        refreshPreview();
        saveCurrentSidecar();
    }
}

bool DocumentController::exportCurrent(const ExportSettings& settings, const QString& outputPath) {
    if (!m_raw.valid) return false;
    ExportEngine engine;
    return engine.exportImage(m_raw.linearRgb, m_editGraph.current(), settings, outputPath,
                              m_raw.metadata.wbCoeffs, m_raw.metadata.rgbCam,
                              m_raw.metadata.isCameraLinear);
}

bool DocumentController::exportBatch(const ExportSettings& settings) {
    ExportEngine engine;
    ExportSettings perImageSettings = settings;
    bool allOk = true;
    QDir().mkpath(settings.outputDir);
    const QString tmpl = settings.fileNameTemplate.isEmpty() ? QStringLiteral("{name}")
                                                             : settings.fileNameTemplate;
    int seq = 0;
    for (const QString& path : m_paths) {
        ++seq;
        RawImage raw = m_decoder.decode(path, 4096);
        if (!raw.valid) { allOk = false; continue; }
        DevelopSettings develop = DevelopSettings::defaults();
        SidecarIO::load(path, develop);
        perImageSettings.watermark = develop.watermark.enabled ? develop.watermark : settings.watermark;
        const QFileInfo fi(path);
        QString base = tmpl;
        base.replace(QStringLiteral("{name}"), fi.completeBaseName());
        base.replace(QStringLiteral("{seq}"), QString::number(seq).rightJustified(4, QLatin1Char('0')));
        const QString out = QDir(settings.outputDir).filePath(
            base + QStringLiteral(".") + settings.format);
        if (!engine.exportImage(raw.linearRgb, develop, perImageSettings, out,
                                raw.metadata.wbCoeffs, raw.metadata.rgbCam,
                                raw.metadata.isCameraLinear))
            allOk = false;
    }
    return allOk;
}

QImage DocumentController::thumbnailForPath(const QString& path) const {
    QImage cached = m_thumbCache.load(path, 256);
    if (!cached.isNull()) return cached;
    RawImage raw = m_decoder.decode(path, 512);
    if (!raw.valid) return {};
    DevelopPipeline pipeline;
    QImage thumb = pipeline.renderLinear(raw.linearRgb, DevelopSettings::defaults(),
                                         raw.metadata.wbCoeffs, raw.metadata.rgbCam, 256,
                                         raw.metadata.isCameraLinear);
    if (!thumb.isNull())
        m_thumbCache.store(path, 256, thumb);
    return thumb;
}

} // namespace mylr