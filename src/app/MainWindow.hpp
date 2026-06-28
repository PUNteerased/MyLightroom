#pragma once

#include "DocumentController.hpp"
#include "../core/PresetManager.hpp"
#include "../ui/DevelopPanel.hpp"
#include "../ui/FilmstripWidget.hpp"
#include "../ui/HistogramWidget.hpp"
#include "../ui/ImageViewport.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QListWidget>

class QLabel;
class QStackedWidget;
class QSplitter;
class QToolButton;
class QTreeWidget;

namespace mylr {

class LibraryGridView;
class AICompareView;
class NavigatorLabel;

enum class AppModule { Library, Develop, AI };

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Imports a folder directly (used by the menu and the Folders tree).
    void importFolderPath(const QString& dir);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onImportFolder();
    void onOpenFile();
    void onSaveSidecar();
    void onPreviewUpdated();
    void onFilmstripSelected(int index);
    void onLibrarySelected(int index);
    void onDevelopChanged();
    void onUndo();
    void onRedo();
    void onSaveReferenceProfile();
    void onApplyMatch();
    void onApplyMatchBatch();
    void onAutoTone();
    void onAutoExposure();
    void onAutoWhiteBalance();
    void onExportCurrent();
    void onExportBatch();
    void onLoadWatermark();
    void onImportXmpPreset();
    void onImportLutPreset();
    void onImportPresetBundle();
    void onAutoStraighten();
    void onCopySettings();
    void onPasteSettings();
    void onResetSettings();
    void onToggleCrop();
    void onToggleBeforeAfter();
    void onToggleReferenceMode();
    void onMatchTotalExposures();

private:
    void setupTheme();
    void setupMenu();
    QWidget* buildModuleBar();
    QWidget* buildLeftPanel();
    QWidget* buildRightPanel();
    QWidget* buildCenter();
    void buildShell();

    void switchModule(AppModule mod);
    void updateHistogram();
    void updateNavigator();
    void updateMetadata();
    void refreshPresetsList();
    void refreshSnapshotsList();
    void refreshHistoryList();
    void refreshCatalogTrees();
    void showImagePaths(const QStringList& paths);
    void refreshFilmstripThumbnails();
    void finalizePresetImport(const PresetImportResult& result);

    DocumentController m_doc;
    AppModule m_module = AppModule::Develop;

    // Center
    QStackedWidget* m_centerStack = nullptr;
    ImageViewport* m_viewport = nullptr;
    LibraryGridView* m_libraryGrid = nullptr;
    AICompareView* m_aiView = nullptr;

    // Shared panels
    HistogramWidget* m_histogram = nullptr;
    DevelopPanel* m_developPanel = nullptr;
    FilmstripWidget* m_filmstrip = nullptr;
    NavigatorLabel* m_navigator = nullptr;

    // Left/right module-specific content stacks
    QStackedWidget* m_leftStack = nullptr;
    QStackedWidget* m_rightStack = nullptr;
    QWidget* m_leftPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QSplitter* m_splitter = nullptr;

    // Library widgets
    QLabel* m_metadataLabel = nullptr;
    QTreeWidget* m_foldersTree = nullptr;
    QTreeWidget* m_collectionsTree = nullptr;

    // Develop side widgets
    QTreeWidget* m_presetsTree = nullptr;
    QListWidget* m_snapshotsList = nullptr;
    QListWidget* m_historyList = nullptr;

    // AI widgets
    QLabel* m_aiInfoLabel = nullptr;

    // Module picker buttons
    QToolButton* m_libBtn = nullptr;
    QToolButton* m_devBtn = nullptr;
    QToolButton* m_aiBtn = nullptr;
    QToolButton* m_exportBtn = nullptr;

    // Copy/paste clipboard for develop settings
    DevelopSettings m_clipboard;
    bool m_hasClipboard = false;

    // Background thumbnail decoding so importing large folders never blocks the UI.
    QFutureWatcher<QImage> m_thumbWatcher;
    QStringList m_thumbPaths;

    // Basic tone settings captured at the start of a histogram drag, so the live
    // delta is applied relative to the value when the drag began.
    BasicSettings m_histDragStart;
};

} // namespace mylr
