#include "MainWindow.hpp"

#include "../export/ExportEngine.hpp"
#include "../render/StraightenDetector.hpp"
#include "../ui/AICompareView.hpp"
#include "../ui/CollapsiblePanel.hpp"
#include "../ui/ExportDialog.hpp"
#include "../ui/LibraryGridView.hpp"
#include "../ui/NavigatorLabel.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QProcess>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace mylr {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupTheme();
    buildShell();
    setupMenu();

    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           QStringLiteral("/catalog.db");
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    m_doc.openCatalog(dbPath);
    connect(m_doc.catalog(), &Catalog::catalogChanged, this, &MainWindow::refreshCatalogTrees);
    refreshCatalogTrees();

    connect(&m_doc, &DocumentController::previewUpdated, this, &MainWindow::onPreviewUpdated);
    connect(&m_doc, &DocumentController::imageLoaded, this, [this](int index) {
        m_filmstrip->setCurrentIndex(index);
        m_libraryGrid->setCurrentImageIndex(index);
        m_developPanel->refreshFromGraph();
        refreshHistoryList();
        refreshSnapshotsList();
        updateMetadata();
        // NOTE: do not stamp currentPreview() here — at imageLoaded time the new
        // image's render is still async, so currentPreview() holds the PREVIOUS
        // image (this was the "previous photo sticks" bug). The selected cell's
        // thumbnail is refreshed from the real render in onPreviewUpdated.
    });
    connect(&m_doc, &DocumentController::errorOccurred, this,
            [this](const QString& message) { statusBar()->showMessage(message, 8000); });
    connect(m_doc.editGraph(), &EditGraph::historyChanged, this, &MainWindow::refreshHistoryList);
    connect(m_doc.presetManager(), &PresetManager::presetsChanged, this,
            &MainWindow::refreshPresetsList);
    connect(m_doc.matchEngine(), &MatchEngine::matchProgress, this, [this](int cur, int total) {
        statusBar()->showMessage(QStringLiteral("Matching %1/%2...").arg(cur).arg(total));
    });
    // Background "Apply Match to All": stream thumbnails + progress to the UI.
    connect(&m_doc, &DocumentController::matchProgress, this, [this](int done, int total) {
        statusBar()->showMessage(QStringLiteral("AI matching %1/%2...").arg(done).arg(total));
    });
    connect(&m_doc, &DocumentController::matchThumbnailReady, this,
            [this](int index, const QImage& thumb) {
                m_filmstrip->setThumbnail(index, thumb);
                m_libraryGrid->setThumbnail(index, thumb);
            });
    connect(&m_doc, &DocumentController::referencePhotoSet, this,
            [this](int index, const QImage& refImage) {
                if (m_aiView) m_aiView->setReferenceImage(refImage);
                m_filmstrip->setReferenceIndex(index);
                m_libraryGrid->setReferenceIndex(index);
                const auto& paths = m_doc.imagePaths();
                const QString name = (index >= 0 && index < paths.size())
                                         ? QFileInfo(paths[index]).fileName()
                                         : QString();
                statusBar()->showMessage(QStringLiteral("Reference photo set: %1").arg(name), 4000);
            });
    connect(&m_doc, &DocumentController::matchCompleted, this, [this](int count, float avg) {
        statusBar()->showMessage(
            QStringLiteral("AI match complete: %1 images (avg confidence %2%)")
                .arg(count)
                .arg(static_cast<int>(avg * 100)),
            5000);
    });

    // Background thumbnails: apply each decoded thumbnail as it becomes ready.
    connect(&m_thumbWatcher, &QFutureWatcher<QImage>::resultReadyAt, this, [this](int index) {
        if (index < 0 || index >= m_thumbPaths.size()) return;
        const QImage thumb = m_thumbWatcher.resultAt(index);
        if (thumb.isNull()) return;
        m_filmstrip->setThumbnail(index, thumb);
        m_libraryGrid->setThumbnail(index, thumb);
    });
    // Lightroom-style keyboard shortcuts.
    new QShortcut(QKeySequence(Qt::Key_G), this, [this] { switchModule(AppModule::Library); });
    new QShortcut(QKeySequence(Qt::Key_D), this, [this] { switchModule(AppModule::Develop); });
    new QShortcut(QKeySequence(Qt::Key_R), this, [this] { onToggleCrop(); });
    new QShortcut(QKeySequence(Qt::Key_Backslash), this, [this] { onToggleBeforeAfter(); });

    switchModule(AppModule::Develop);
    refreshPresetsList();
    setWindowTitle(QStringLiteral("MyLightroom"));
    resize(1500, 950);
}

void MainWindow::setupTheme() {
    qApp->setStyle(QStringLiteral("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(38, 38, 38));
    p.setColor(QPalette::WindowText, QColor(210, 210, 210));
    p.setColor(QPalette::Base, QColor(30, 30, 30));
    p.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    p.setColor(QPalette::Button, QColor(58, 58, 58));
    p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    p.setColor(QPalette::Highlight, QColor(70, 120, 200));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
    p.setColor(QPalette::ToolTipText, Qt::white);
    qApp->setPalette(p);
    qApp->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #1f1f1f; border: 1px solid #333; }"
        "QLabel { color: #cfcfcf; }"
        "QPushButton { background-color: #3a3a3a; border: 1px solid #4a4a4a; padding: 4px 8px;"
        " border-radius: 3px; }"
        "QPushButton:hover { background-color: #464646; }"
        "QSlider::groove:horizontal { height: 4px; background: #555; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; background: #cfcfcf; margin: -5px 0;"
        " border-radius: 6px; }"
        "QScrollBar:vertical { background: #2a2a2a; width: 10px; }"
        "QScrollBar::handle:vertical { background: #555; border-radius: 4px; min-height: 20px; }"));
}

QWidget* MainWindow::buildModuleBar() {
    auto* bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background-color: #1c1c1c;"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);

    auto* logo = new QLabel(QStringLiteral("My Lightroom"));
    logo->setStyleSheet(QStringLiteral("color:#bbbbbb; font-weight:bold; font-size:14px;"));
    h->addWidget(logo);
    h->addStretch();

    auto makeBtn = [&](const QString& text) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { color:#9a9a9a; border:none; padding:6px 14px; font-weight:bold; }"
            "QToolButton:checked { color:#ffffff; }"
            "QToolButton:hover { color:#dddddd; }"));
        h->addWidget(b);
        return b;
    };
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    m_libBtn = makeBtn(QStringLiteral("Library"));
    m_devBtn = makeBtn(QStringLiteral("Develop"));
    m_aiBtn = makeBtn(QStringLiteral("AI"));
    m_exportBtn = makeBtn(QStringLiteral("Export"));
    m_exportBtn->setCheckable(false);  // acts as a launcher, not a persistent view
    connect(m_libBtn, &QToolButton::clicked, this, [this] { switchModule(AppModule::Library); });
    connect(m_devBtn, &QToolButton::clicked, this, [this] { switchModule(AppModule::Develop); });
    connect(m_aiBtn, &QToolButton::clicked, this, [this] { switchModule(AppModule::AI); });
    connect(m_exportBtn, &QToolButton::clicked, this, &MainWindow::onExportCurrent);
    return bar;
}

QWidget* MainWindow::buildCenter() {
    m_centerStack = new QStackedWidget;

    m_libraryGrid = new LibraryGridView;
    connect(m_libraryGrid, &LibraryGridView::imageActivated, this, &MainWindow::onLibrarySelected);
    connect(m_libraryGrid, &LibraryGridView::imageClicked, this,
            [this](int index) { m_doc.loadImage(index); });
    connect(m_libraryGrid, &LibraryGridView::ratingChanged, this, [this](int index, int rating) {
        const auto& paths = m_doc.imagePaths();
        if (index >= 0 && index < paths.size())
            m_doc.catalog()->updateRating(paths[index], rating);
    });
    connect(m_libraryGrid, &LibraryGridView::colorLabelChanged, this,
            [this](int index, const QString& label) {
                const auto& paths = m_doc.imagePaths();
                if (index >= 0 && index < paths.size())
                    m_doc.catalog()->updateColorLabel(paths[index], label);
            });
    connect(m_libraryGrid, &LibraryGridView::setAsReferenceRequested, this, [this](int index) {
        if (!m_doc.setAsReferencePhoto(index))
            QMessageBox::warning(this, QStringLiteral("Reference"),
                                 QStringLiteral("Could not set reference photo"));
    });
    connect(m_libraryGrid, &LibraryGridView::createVirtualCopyRequested, this, [this](int index) {
        const auto& paths = m_doc.imagePaths();
        if (index >= 0 && index < paths.size()) {
            m_doc.catalog()->createVirtualCopy(paths[index]);
            statusBar()->showMessage(QStringLiteral("Virtual copy created"), 3000);
        }
    });
    connect(m_libraryGrid, &LibraryGridView::showInExplorerRequested, this, [this](int index) {
        const auto& paths = m_doc.imagePaths();
        if (index >= 0 && index < paths.size())
            QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(paths[index])});
    });
    connect(m_libraryGrid, &LibraryGridView::exportRequested, this, [this](int index) {
        m_doc.loadImage(index);
        onExportCurrent();
    });
    connect(m_libraryGrid, &LibraryGridView::addToCollectionRequested, this, [this]() {
        const QList<int> sel = m_libraryGrid->selectedImageIndices();
        if (sel.isEmpty()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Add to Collection"),
                                                   QStringLiteral("Collection name:"),
                                                   QLineEdit::Normal, QStringLiteral("Untitled"), &ok);
        if (!ok || name.isEmpty()) return;
        const QString id = m_doc.catalog()->createCollection(name);
        const auto& paths = m_doc.imagePaths();
        for (int idx : sel)
            if (idx >= 0 && idx < paths.size())
                m_doc.catalog()->addImageToCollection(id, paths[idx]);
        statusBar()->showMessage(
            QStringLiteral("Added %1 photo(s) to '%2'").arg(sel.size()).arg(name), 3000);
    });

    m_viewport = new ImageViewport;
    connect(m_viewport, &ImageViewport::viewChanged, this, [this] { updateNavigator(); });
    m_viewport->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_viewport, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Reset All"), this, &MainWindow::onResetSettings);
        menu.addAction(QStringLiteral("Copy Settings"), this, &MainWindow::onCopySettings);
        menu.addAction(QStringLiteral("Paste Settings"), this, &MainWindow::onPasteSettings);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Set as Reference Photo"), this,
                       &MainWindow::onSaveReferenceProfile);
        menu.addAction(QStringLiteral("Match Total Exposures"), this,
                       &MainWindow::onMatchTotalExposures);
        menu.addAction(QStringLiteral("Auto White Balance"), this, &MainWindow::onAutoWhiteBalance);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Reset Crop"), this, [this] {
            auto s = m_doc.editGraph()->mutableCurrent();
            s.geometry.cropLeft = 0.f;
            s.geometry.cropTop = 0.f;
            s.geometry.cropRight = 1.f;
            s.geometry.cropBottom = 1.f;
            m_doc.editGraph()->setSettings(s, QStringLiteral("Reset Crop"));
            m_viewport->setCropGeometry(s.geometry);
            m_doc.refreshPreview();
        });
        menu.exec(m_viewport->mapToGlobal(pos));
    });
    connect(m_viewport, &ImageViewport::cropGeometryChanged, this,
            [this](const GeometrySettings& geom) {
                auto s = m_doc.editGraph()->mutableCurrent();
                s.geometry.cropLeft = geom.cropLeft;
                s.geometry.cropTop = geom.cropTop;
                s.geometry.cropRight = geom.cropRight;
                s.geometry.cropBottom = geom.cropBottom;
                m_doc.editGraph()->setSettings(s, QStringLiteral("Crop"));
                m_doc.requestInteractivePreview();
            });

    m_aiView = new AICompareView;

    m_centerStack->addWidget(m_libraryGrid);  // 0 Library
    m_centerStack->addWidget(m_viewport);      // 1 Develop
    m_centerStack->addWidget(m_aiView);        // 2 AI
    return m_centerStack;
}

QWidget* MainWindow::buildLeftPanel() {
    m_leftPanel = new QWidget;
    m_leftPanel->setFixedWidth(240);
    m_leftPanel->setStyleSheet(QStringLiteral("background-color: #2b2b2b;"));
    auto* v = new QVBoxLayout(m_leftPanel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Shared navigator (interactive: shows the visible region, click/drag to pan).
    m_navigator = new NavigatorLabel;
    m_navigator->setFixedHeight(150);
    m_navigator->setAlignment(Qt::AlignCenter);
    m_navigator->setStyleSheet(QStringLiteral("background-color: #1a1a1a;"));
    m_navigator->setCursor(Qt::PointingHandCursor);
    m_navigator->onNavigate = [this](QPointF n) {
        if (m_viewport) m_viewport->centerViewOnNormalized(n);
    };
    auto* navPanel = new CollapsiblePanel(QStringLiteral("Navigator"));
    navPanel->setContentWidget(m_navigator);
    v->addWidget(navPanel);

    m_leftStack = new QStackedWidget;

    // Library left: Folders + Collections (wired to the SQLite catalog).
    {
        auto* page = new QWidget;
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(0);

        m_foldersTree = new QTreeWidget;
        m_foldersTree->setHeaderHidden(true);
        m_foldersTree->setIndentation(12);
        connect(m_foldersTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
            const QString path = item->data(0, Qt::UserRole).toString();
            if (!path.isEmpty()) importFolderPath(path);
        });
        auto* foldersPanel = new CollapsiblePanel(QStringLiteral("Folders"));
        foldersPanel->setContentWidget(m_foldersTree);
        pv->addWidget(foldersPanel);

        m_collectionsTree = new QTreeWidget;
        m_collectionsTree->setHeaderHidden(true);
        m_collectionsTree->setIndentation(12);
        connect(m_collectionsTree, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem* item, int) {
                    const QString id = item->data(0, Qt::UserRole).toString();
                    if (!id.isEmpty())
                        showImagePaths(m_doc.catalog()->imagesInCollection(id));
                });
        auto* collectionsPanel = new CollapsiblePanel(QStringLiteral("Collections"));
        collectionsPanel->setContentWidget(m_collectionsTree);
        pv->addWidget(collectionsPanel);
        pv->addStretch();
        m_leftStack->addWidget(page);
    }
    // Develop left: Presets + Snapshots + History.
    {
        auto* page = new QWidget;
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(0);

        m_presetsTree = new QTreeWidget;
        m_presetsTree->setHeaderHidden(true);
        m_presetsTree->setIndentation(12);
        connect(m_presetsTree, &QTreeWidget::itemDoubleClicked, this,
                [this](QTreeWidgetItem* item, int) {
                    const QString id = item->data(0, Qt::UserRole).toString();
                    if (!id.isEmpty()) m_doc.applyPreset(id);
                });
        auto* presetPanel = new CollapsiblePanel(QStringLiteral("Presets"));
        presetPanel->setContentWidget(m_presetsTree);
        pv->addWidget(presetPanel);

        m_snapshotsList = new QListWidget;
        connect(m_snapshotsList, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem* item) {
                    m_doc.editGraph()->restoreSnapshot(item->data(Qt::UserRole).toString());
                    m_doc.refreshPreview();
                });
        auto* snapWrap = new QWidget;
        auto* snapV = new QVBoxLayout(snapWrap);
        snapV->setContentsMargins(0, 0, 0, 0);
        snapV->addWidget(m_snapshotsList);
        auto* addSnap = new QPushButton(QStringLiteral("Add Snapshot"));
        connect(addSnap, &QPushButton::clicked, this, [this] {
            m_doc.editGraph()->saveSnapshot(
                QStringLiteral("Snapshot %1").arg(m_doc.editGraph()->snapshots().size() + 1));
            refreshSnapshotsList();
        });
        snapV->addWidget(addSnap);
        auto* snapPanel = new CollapsiblePanel(QStringLiteral("Snapshots"));
        snapPanel->setContentWidget(snapWrap);
        pv->addWidget(snapPanel);

        m_historyList = new QListWidget;
        // Click-to-jump: each row maps directly to an EditGraph history state.
        connect(m_historyList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            m_doc.editGraph()->jumpTo(m_historyList->row(item));
            m_doc.refreshPreview();
        });
        auto* histPanel = new CollapsiblePanel(QStringLiteral("History"));
        histPanel->setContentWidget(m_historyList);
        pv->addWidget(histPanel);
        pv->addStretch();
        m_leftStack->addWidget(page);
    }
    // AI left: info.
    {
        auto* page = new QWidget;
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(8, 8, 8, 8);
        auto* info = new QLabel(QStringLiteral(
            "AI Mode\n\nCapture a reference look from an edited image, then match it "
            "across your shoot. Or apply automatic tone, exposure and white balance."));
        info->setWordWrap(true);
        pv->addWidget(info);
        pv->addStretch();
        m_leftStack->addWidget(page);
    }

    v->addWidget(m_leftStack, 1);
    return m_leftPanel;
}

QWidget* MainWindow::buildRightPanel() {
    m_rightPanel = new QWidget;
    // Fixed 320px to match Lightroom Classic; the Develop slider rows reserve a
    // value column so numbers stay visible at this width.
    m_rightPanel->setFixedWidth(320);
    m_rightPanel->setStyleSheet(QStringLiteral("background-color: #2b2b2b;"));
    auto* v = new QVBoxLayout(m_rightPanel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_histogram = new HistogramWidget;
    m_histogram->setFixedHeight(120);
    v->addWidget(m_histogram);

    // Direct histogram manipulation: drag zones adjust Blacks/Exposure/Whites.
    connect(m_histogram, &HistogramWidget::toneDragBegin, this,
            [this](int) { m_histDragStart = m_doc.editGraph()->current().basic; });
    connect(m_histogram, &HistogramWidget::toneDragged, this, [this](int zone, int dy) {
        if (!m_doc.currentRaw().valid) return;
        // Drag up (negative dy) brightens. Scale: ~1 EV or ~50 units per 100px.
        const float d = static_cast<float>(-dy);
        auto s = m_doc.editGraph()->current();
        s.basic = m_histDragStart;
        if (zone == HistogramWidget::ZoneExposure)
            s.basic.exposure = qBound(-5.f, m_histDragStart.exposure + d * 0.01f, 5.f);
        else if (zone == HistogramWidget::ZoneBlacks)
            s.basic.blacks = qBound(-100.f, m_histDragStart.blacks + d * 0.5f, 100.f);
        else
            s.basic.whites = qBound(-100.f, m_histDragStart.whites + d * 0.5f, 100.f);
        m_doc.editGraph()->setSettingsLive(s);
        m_doc.requestInteractivePreview();
    });
    connect(m_histogram, &HistogramWidget::toneDragEnd, this, [this]() {
        // Commit one history entry and refresh the panel + full-res render.
        m_doc.editGraph()->setSettings(m_doc.editGraph()->current(),
                                       QStringLiteral("Histogram Adjust"));
        m_developPanel->refreshFromGraph();
        m_doc.refreshPreview();
    });

    m_rightStack = new QStackedWidget;

    // Library right: Quick Develop + Metadata.
    {
        auto* page = new QWidget;
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(0);

        auto* quick = new QWidget;
        auto* qg = new QVBoxLayout(quick);
        qg->setContentsMargins(6, 6, 6, 6);
        auto* autoTone = new QPushButton(QStringLiteral("Auto Tone"));
        connect(autoTone, &QPushButton::clicked, this, &MainWindow::onAutoTone);
        qg->addWidget(autoTone);
        auto* row = new QWidget;
        auto* rh = new QHBoxLayout(row);
        rh->setContentsMargins(0, 0, 0, 0);
        auto* expMinus = new QPushButton(QStringLiteral("Exp -"));
        auto* expPlus = new QPushButton(QStringLiteral("Exp +"));
        connect(expMinus, &QPushButton::clicked, this, [this] {
            auto s = m_doc.editGraph()->mutableCurrent();
            s.basic.exposure = qBound(-5.f, s.basic.exposure - 0.33f, 5.f);
            m_doc.editGraph()->setSettings(s, QStringLiteral("Exposure -"));
            m_doc.refreshPreview();
        });
        connect(expPlus, &QPushButton::clicked, this, [this] {
            auto s = m_doc.editGraph()->mutableCurrent();
            s.basic.exposure = qBound(-5.f, s.basic.exposure + 0.33f, 5.f);
            m_doc.editGraph()->setSettings(s, QStringLiteral("Exposure +"));
            m_doc.refreshPreview();
        });
        rh->addWidget(expMinus);
        rh->addWidget(expPlus);
        qg->addWidget(row);
        auto* quickPanel = new CollapsiblePanel(QStringLiteral("Quick Develop"));
        quickPanel->setContentWidget(quick);
        pv->addWidget(quickPanel);

        m_metadataLabel = new QLabel(QStringLiteral("No image"));
        m_metadataLabel->setWordWrap(true);
        m_metadataLabel->setContentsMargins(6, 4, 6, 4);
        auto* metaPanel = new CollapsiblePanel(QStringLiteral("Metadata"));
        metaPanel->setContentWidget(m_metadataLabel);
        pv->addWidget(metaPanel);
        pv->addStretch();
        m_rightStack->addWidget(page);
    }
    // Develop right: full DevelopPanel.
    {
        m_developPanel = new DevelopPanel(m_doc.editGraph());
        connect(m_developPanel, &DevelopPanel::developChanged, this, &MainWindow::onDevelopChanged);
        m_rightStack->addWidget(m_developPanel);
    }
    // AI right: controls.
    {
        auto* page = new QWidget;
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(8, 8, 8, 8);
        pv->setSpacing(6);

        m_aiInfoLabel = new QLabel(QStringLiteral("Scene: -"));
        m_aiInfoLabel->setWordWrap(true);
        pv->addWidget(m_aiInfoLabel);

        auto* autoTone = new QPushButton(QStringLiteral("Auto Tone"));
        connect(autoTone, &QPushButton::clicked, this, &MainWindow::onAutoTone);
        auto* autoExp = new QPushButton(QStringLiteral("Auto Exposure"));
        connect(autoExp, &QPushButton::clicked, this, &MainWindow::onAutoExposure);
        auto* autoWb = new QPushButton(QStringLiteral("Auto White Balance"));
        connect(autoWb, &QPushButton::clicked, this, &MainWindow::onAutoWhiteBalance);
        pv->addWidget(autoTone);
        pv->addWidget(autoExp);
        pv->addWidget(autoWb);

        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color:#444;"));
        pv->addWidget(line);

        auto* capture = new QPushButton(QStringLiteral("Set as Reference Photo"));
        connect(capture, &QPushButton::clicked, this, &MainWindow::onSaveReferenceProfile);
        auto* applyOne = new QPushButton(QStringLiteral("Apply Match to Current"));
        connect(applyOne, &QPushButton::clicked, this, &MainWindow::onApplyMatch);
        auto* applyAll = new QPushButton(QStringLiteral("Apply Match to All"));
        connect(applyAll, &QPushButton::clicked, this, &MainWindow::onApplyMatchBatch);
        pv->addWidget(capture);
        pv->addWidget(applyOne);
        pv->addWidget(applyAll);
        pv->addStretch();
        m_rightStack->addWidget(page);
    }

    v->addWidget(m_rightStack, 1);
    return m_rightPanel;
}

void MainWindow::buildShell() {
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(buildModuleBar());

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(buildLeftPanel());
    m_splitter->addWidget(buildCenter());
    m_splitter->addWidget(buildRightPanel());
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({240, 900, 320});
    mainLayout->addWidget(m_splitter, 1);

    m_filmstrip = new FilmstripWidget;
    connect(m_filmstrip, &FilmstripWidget::imageSelected, this, &MainWindow::onFilmstripSelected);

    // Filmstrip strip with prev/next paging buttons so editing many photos does
    // not require slow mouse-wheel scrolling.
    auto* filmstripRow = new QWidget;
    filmstripRow->setFixedHeight(112);
    filmstripRow->setStyleSheet(QStringLiteral("background-color: #1e1e1e;"));
    auto* filmstripLayout = new QHBoxLayout(filmstripRow);
    filmstripLayout->setContentsMargins(0, 0, 0, 0);
    filmstripLayout->setSpacing(0);
    auto makeNavButton = [this](const QString& glyph, int step) {
        auto* btn = new QToolButton;
        btn->setText(glyph);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedWidth(30);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{color:#e0e0e0; background:#333333; border:none; border-radius:0;"
            " font-size:22px; font-weight:bold;}"
            "QToolButton:hover{background:#4a4a4a; color:#ffffff;}"
            "QToolButton:pressed{background:#5a86c8;}"));
        connect(btn, &QToolButton::clicked, m_filmstrip,
                [this, step] { m_filmstrip->scrollByThumbs(step); });
        return btn;
    };
    filmstripLayout->addWidget(makeNavButton(QStringLiteral("\u2039"), -5));
    filmstripLayout->addWidget(m_filmstrip, 1);
    filmstripLayout->addWidget(makeNavButton(QStringLiteral("\u203a"), 5));
    mainLayout->addWidget(filmstripRow);

    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::setupMenu() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Import Folder..."), this, &MainWindow::onImportFolder);
    fileMenu->addAction(QStringLiteral("&Open RAW..."), this, &MainWindow::onOpenFile);
    fileMenu->addAction(QStringLiteral("&Save Sidecar"), this, &MainWindow::onSaveSidecar);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Export &Current..."), this, &MainWindow::onExportCurrent);
    fileMenu->addAction(QStringLiteral("Export &Batch..."), this, &MainWindow::onExportBatch);
    fileMenu->addAction(QStringLiteral("Load &Watermark..."), this, &MainWindow::onLoadWatermark);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Move Current File..."), this, [this] {
        const int idx = m_doc.currentIndex();
        const auto& paths = m_doc.imagePaths();
        if (idx < 0 || idx >= paths.size()) return;
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Move To Folder"));
        if (dir.isEmpty()) return;
        if (m_doc.catalog()->moveImageFile(paths[idx], dir))
            statusBar()->showMessage(QStringLiteral("File moved"), 3000);
        else
            QMessageBox::warning(this, QStringLiteral("Move"), QStringLiteral("Could not move file"));
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("&Undo"), this, &MainWindow::onUndo, QKeySequence::Undo);
    editMenu->addAction(QStringLiteral("&Redo"), this, &MainWindow::onRedo, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("&Copy Settings"), this, &MainWindow::onCopySettings,
                        QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    editMenu->addAction(QStringLiteral("&Paste Settings"), this, &MainWindow::onPasteSettings,
                        QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    editMenu->addAction(QStringLiteral("&Reset"), this, &MainWindow::onResetSettings);

    auto* devMenu = menuBar()->addMenu(QStringLiteral("&Develop"));
    devMenu->addAction(QStringLiteral("Toggle &Crop"), this, &MainWindow::onToggleCrop,
                       QKeySequence(Qt::Key_R));
    devMenu->addAction(QStringLiteral("&Before/After"), this, &MainWindow::onToggleBeforeAfter);
    devMenu->addAction(QStringLiteral("Reference &Photo Mode"), this,
                       &MainWindow::onToggleReferenceMode, QKeySequence(QStringLiteral("Shift+R")));
    devMenu->addAction(QStringLiteral("Auto &Straighten"), this, &MainWindow::onAutoStraighten);

    auto* aiMenu = menuBar()->addMenu(QStringLiteral("&AI"));
    aiMenu->addAction(QStringLiteral("Auto &Tone"), this, &MainWindow::onAutoTone);
    aiMenu->addAction(QStringLiteral("Auto &Exposure"), this, &MainWindow::onAutoExposure);
    aiMenu->addAction(QStringLiteral("Auto &White Balance"), this, &MainWindow::onAutoWhiteBalance);
    aiMenu->addSeparator();
    aiMenu->addAction(QStringLiteral("Set as &Reference Photo"), this,
                      &MainWindow::onSaveReferenceProfile);
    aiMenu->addAction(QStringLiteral("Apply Match to &Current"), this, &MainWindow::onApplyMatch);
    aiMenu->addAction(QStringLiteral("Apply Match to &All"), this, &MainWindow::onApplyMatchBatch);
    aiMenu->addAction(QStringLiteral("Match Total E&xposures"), this,
                      &MainWindow::onMatchTotalExposures,
                      QKeySequence(QStringLiteral("Ctrl+Alt+Shift+M")));

    auto* presetMenu = menuBar()->addMenu(QStringLiteral("&Presets"));
    presetMenu->addAction(QStringLiteral("Import &XMP Preset..."), this,
                          &MainWindow::onImportXmpPreset);
    presetMenu->addAction(QStringLiteral("Import &LUT (.cube/.3dl)..."), this,
                          &MainWindow::onImportLutPreset);
    presetMenu->addAction(QStringLiteral("Import Preset &Bundle (folder)..."), this,
                          &MainWindow::onImportPresetBundle);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_viewport) {
        const qreal dpr = m_viewport->devicePixelRatioF();
        const int edge = static_cast<int>(
            qMax(m_viewport->width(), m_viewport->height()) * dpr);
        m_doc.setPreviewMaxEdge(qBound(640, edge, 2048));
    }
}

void MainWindow::switchModule(AppModule mod) {
    m_module = mod;
    const int idx = static_cast<int>(mod);
    m_centerStack->setCurrentIndex(idx);
    m_leftStack->setCurrentIndex(idx);
    m_rightStack->setCurrentIndex(idx);

    m_libBtn->setChecked(mod == AppModule::Library);
    m_devBtn->setChecked(mod == AppModule::Develop);
    m_aiBtn->setChecked(mod == AppModule::AI);

    if (mod != AppModule::Develop && m_viewport->cropMode())
        m_viewport->setCropMode(false);

    if (mod == AppModule::AI)
        onPreviewUpdated();  // refresh AI compare view
}

void MainWindow::onImportFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Import Folder"));
    if (dir.isEmpty()) return;
    importFolderPath(dir);
}

void MainWindow::importFolderPath(const QString& dir) {
    if (dir.isEmpty()) return;
    const QStringList paths = m_doc.importFolder(dir);
    m_filmstrip->setImages(paths);
    m_libraryGrid->setImages(paths);
    refreshFilmstripThumbnails();
    if (!paths.isEmpty()) {
        m_filmstrip->setCurrentIndex(m_doc.currentIndex());
        m_libraryGrid->setCurrentImageIndex(m_doc.currentIndex());
    }
    statusBar()->showMessage(QStringLiteral("Imported %1 images").arg(paths.size()));
}

void MainWindow::onOpenFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open RAW"), {},
        QStringLiteral("RAW Files (*.cr2 *.cr3 *.nef *.arw *.dng);;All Files (*.*)"));
    if (path.isEmpty()) return;
    m_doc.importFolder(QFileInfo(path).absolutePath());
    m_filmstrip->setImages(m_doc.imagePaths());
    m_libraryGrid->setImages(m_doc.imagePaths());
    refreshFilmstripThumbnails();
    for (int i = 0; i < m_doc.imagePaths().size(); ++i) {
        if (m_doc.imagePaths()[i] == path) {
            m_doc.loadImage(i);
            break;
        }
    }
}

void MainWindow::onSaveSidecar() {
    m_doc.saveCurrentSidecar();
    statusBar()->showMessage(QStringLiteral("Sidecar saved"), 3000);
}

void MainWindow::onPreviewUpdated() {
    m_viewport->setImage(m_doc.currentPreview());
    // Only pay for the "before" render when the compare view actually needs it.
    if (m_viewport->compareMode())
        m_viewport->setBeforeImage(m_doc.beforePreview());
    // Histogram/navigator recompute is expensive; skip it during fast drag
    // updates and let the debounced full-resolution render refresh them.
    if (!m_doc.lastPreviewInteractive()) {
        updateHistogram();
        updateNavigator();
        // Refresh the selected cell's thumbnail from its OWN finished render so
        // the filmstrip/grid always show the correct image (fixes "stick").
        const int ci = m_doc.currentIndex();
        const QImage prev = m_doc.currentPreview();
        if (ci >= 0 && !prev.isNull()) {
            m_filmstrip->setThumbnail(ci, prev);
            m_libraryGrid->setThumbnail(ci, prev);
        }
    }
    if (m_module == AppModule::AI) {
        m_aiView->setBeforeImage(m_doc.beforePreview());
        m_aiView->setAfterImage(m_doc.currentPreview());
        m_aiView->setInfo(m_doc.currentSceneType(), m_doc.lastMatchConfidence());
        if (m_aiInfoLabel) {
            QString info = QStringLiteral("Scene: %1").arg(
                m_doc.currentSceneType().isEmpty() ? QStringLiteral("-") : m_doc.currentSceneType());
            if (m_doc.lastMatchConfidence() >= 0.f)
                info += QStringLiteral("\nMatch confidence: %1%")
                            .arg(static_cast<int>(m_doc.lastMatchConfidence() * 100));
            m_aiInfoLabel->setText(info);
        }
    }
}

void MainWindow::onFilmstripSelected(int index) {
    if (index == m_doc.currentIndex()) return;
    m_doc.loadImage(index);
}

void MainWindow::onLibrarySelected(int index) {
    m_doc.loadImage(index);
    switchModule(AppModule::Develop);
}

void MainWindow::onDevelopChanged() {
    m_doc.requestInteractivePreview();
}

void MainWindow::onUndo() {
    m_doc.editGraph()->undo();
    m_doc.refreshPreview();
}

void MainWindow::onRedo() {
    m_doc.editGraph()->redo();
    m_doc.refreshPreview();
}

void MainWindow::onCopySettings() {
    m_clipboard = m_doc.editGraph()->current();
    m_hasClipboard = true;
    statusBar()->showMessage(QStringLiteral("Settings copied"), 3000);
}

void MainWindow::onPasteSettings() {
    if (!m_hasClipboard) return;
    m_doc.editGraph()->setSettings(m_clipboard, QStringLiteral("Paste Settings"));
    m_doc.refreshPreview();
    m_doc.saveCurrentSidecar();
}

void MainWindow::onResetSettings() {
    m_doc.editGraph()->setSettings(DevelopSettings::defaults(), QStringLiteral("Reset"));
    m_doc.refreshPreview();
    m_doc.saveCurrentSidecar();
}

void MainWindow::onToggleCrop() {
    if (m_module != AppModule::Develop) switchModule(AppModule::Develop);
    const bool enable = !m_viewport->cropMode();
    if (enable)
        m_viewport->setCropGeometry(m_doc.editGraph()->current().geometry);
    m_viewport->setCropMode(enable);
    statusBar()->showMessage(enable ? QStringLiteral("Crop mode on") : QStringLiteral("Crop mode off"),
                             2000);
}

void MainWindow::onToggleBeforeAfter() {
    const bool on = !m_viewport->compareMode();
    if (on)
        m_viewport->setBeforeImage(m_doc.beforePreview());
    m_viewport->setCompareMode(on);
}

void MainWindow::onAutoTone() {
    if (m_doc.applyAutoTone()) statusBar()->showMessage(QStringLiteral("Auto Tone applied"), 3000);
}

void MainWindow::onAutoExposure() {
    if (m_doc.applyAutoExposure())
        statusBar()->showMessage(QStringLiteral("Auto Exposure applied"), 3000);
}

void MainWindow::onAutoWhiteBalance() {
    if (m_doc.applyAutoWhiteBalance())
        statusBar()->showMessage(QStringLiteral("Auto White Balance applied"), 3000);
}

void MainWindow::onSaveReferenceProfile() {
    // Capture the currently-loaded image as the reference photo using the
    // render-then-capture flow (emits referencePhotoSet to update the UI).
    if (!m_doc.setAsReferencePhoto(m_doc.currentIndex()))
        QMessageBox::warning(this, QStringLiteral("Match"), QStringLiteral("No image loaded"));
}

void MainWindow::onMatchTotalExposures() {
    if (m_doc.matchTotalExposures())
        statusBar()->showMessage(QStringLiteral("Matched total exposures to reference"), 3000);
}

void MainWindow::onToggleReferenceMode() {
    if (m_module != AppModule::Develop) switchModule(AppModule::Develop);
    const bool on = !m_viewport->referenceMode();
    if (on) {
        const QImage ref = m_doc.referenceImage();
        if (ref.isNull()) {
            statusBar()->showMessage(QStringLiteral("Capture a reference look first"), 3000);
            return;
        }
        m_viewport->setReferenceImage(ref);
    }
    m_viewport->setReferenceMode(on);
    statusBar()->showMessage(on ? QStringLiteral("Reference Photo Mode on")
                                : QStringLiteral("Reference Photo Mode off"),
                             2000);
}

void MainWindow::onApplyMatch() {
    if (m_doc.applyMatchToCurrent())
        statusBar()->showMessage(QStringLiteral("AI match applied"), 3000);
}

void MainWindow::onApplyMatchBatch() {
    if (m_doc.isBatchMatchRunning()) {
        statusBar()->showMessage(QStringLiteral("A batch match is already running"), 3000);
        return;
    }
    QVector<int> indices;
    for (int i = 0; i < m_doc.imagePaths().size(); ++i) indices.append(i);
    if (indices.isEmpty()) return;
    // Runs on the thread pool; thumbnails/progress arrive via signals.
    m_doc.applyMatchToAllAsync(indices);
}

void MainWindow::onExportCurrent() {
    ExportSettings initial;
    initial.watermark = m_doc.editGraph()->current().watermark;
    ExportDialog dlg(initial, false, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ExportSettings s = dlg.settings();

    QString ext = s.format;
    if (ext == QStringLiteral("jpeg")) ext = QStringLiteral("jpg");
    else if (ext == QStringLiteral("tiff")) ext = QStringLiteral("tif");
    QString suggested;
    const int idx = m_doc.currentIndex();
    const auto& paths = m_doc.imagePaths();
    if (idx >= 0 && idx < paths.size()) {
        const QFileInfo fi(paths[idx]);
        const QString dir = s.outputDir.isEmpty() ? fi.absolutePath() : s.outputDir;
        suggested = QDir(dir).filePath(fi.completeBaseName() + QStringLiteral(".") + ext);
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export As"), suggested,
                                                      QStringLiteral("Images (*.%1)").arg(ext));
    if (path.isEmpty()) return;
    if (m_doc.exportCurrent(s, path))
        statusBar()->showMessage(QStringLiteral("Exported"), 3000);
}

void MainWindow::onExportBatch() {
    ExportSettings initial;
    initial.watermark = m_doc.editGraph()->current().watermark;
    ExportDialog dlg(initial, true, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ExportSettings s = dlg.settings();
    if (s.outputDir.isEmpty()) {
        s.outputDir = QFileDialog::getExistingDirectory(this, QStringLiteral("Export Folder"));
        if (s.outputDir.isEmpty()) return;
    }
    if (m_doc.exportBatch(s))
        statusBar()->showMessage(QStringLiteral("Batch export complete"), 3000);
}

void MainWindow::onLoadWatermark() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Watermark PNG"), {}, QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) return;
    auto s = m_doc.editGraph()->mutableCurrent();
    s.watermark.imagePath = path;
    s.watermark.enabled = true;
    m_doc.editGraph()->setSettings(s, QStringLiteral("Watermark"));
}

void MainWindow::finalizePresetImport(const PresetImportResult& result) {
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("Import failed"), result.message);
        return;
    }
    m_doc.presetManager()->savePreset(result.preset, QStringLiteral("presets"));
    refreshPresetsList();

    QString body = result.message;
    if (!result.xmpReport.mappedFields.isEmpty()) {
        body += QStringLiteral("\n\nMapped (%1):\n").arg(result.xmpReport.mappedFields.size());
        body += result.xmpReport.mappedFields.mid(0, 12).join(QStringLiteral(", "));
        if (result.xmpReport.mappedFields.size() > 12) body += QStringLiteral("...");
    }
    if (result.lutResult.success)
        body += QStringLiteral("\n\nLUT: %1 (%2\u00b3)")
                    .arg(result.lutResult.storedPath)
                    .arg(result.lutResult.size);
    if (!result.xmpReport.warnings.isEmpty())
        body += QStringLiteral("\n\nNotes:\n") + result.xmpReport.warnings.join(QStringLiteral("\n"));

    QMessageBox::information(this, QStringLiteral("Import complete"), body);
    statusBar()->showMessage(QStringLiteral("Imported preset: %1").arg(result.preset.name), 5000);
}

void MainWindow::onImportXmpPreset() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import XMP Preset"), {},
        QStringLiteral("Lightroom Preset (*.xmp);;All Files (*.*)"));
    if (path.isEmpty()) return;
    const QString lutDir = QFileInfo(path).absolutePath();
    const PresetImportResult result = m_doc.presetManager()->importXmp(path, lutDir);
    if (result.success)
        finalizePresetImport(result);
    else
        QMessageBox::warning(this, QStringLiteral("Import XMP"), result.message);
}

void MainWindow::onImportLutPreset() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import LUT"), {},
        QStringLiteral("LUT Files (*.cube *.3dl);;Cube (*.cube);;3DL (*.3dl)"));
    if (path.isEmpty()) return;
    const PresetImportResult result = m_doc.presetManager()->importLutPreset(path, 1.f);
    finalizePresetImport(result);
}

void MainWindow::onImportPresetBundle() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Import Preset Bundle"), {}, QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;
    const PresetImportResult result = m_doc.presetManager()->importBundle(dir);
    if (result.success) {
        refreshPresetsList();
        QMessageBox::information(this, QStringLiteral("Bundle Import"), result.message);
        statusBar()->showMessage(result.message, 5000);
    } else {
        QMessageBox::warning(this, QStringLiteral("Bundle Import"), result.message);
    }
}

void MainWindow::onAutoStraighten() {
    StraightenDetector detector;
    const float angle = detector.detectAngle(m_doc.currentPreview());
    auto s = m_doc.editGraph()->mutableCurrent();
    s.geometry.straighten = -angle;
    m_doc.editGraph()->setSettings(s, QStringLiteral("Auto Straighten"));
    m_doc.refreshPreview();
}

void MainWindow::updateHistogram() {
    DevelopPipeline pipe;
    m_histogram->setHistogram(pipe.computeHistogram(m_doc.currentPreview()));
}

void MainWindow::updateNavigator() {
    if (!m_navigator) return;
    const QImage preview = m_doc.currentPreview();
    if (preview.isNull()) {
        m_navigator->clear();
        return;
    }
    m_navigator->setPixmap(QPixmap::fromImage(preview).scaled(
        m_navigator->width() - 8, m_navigator->height() - 8, Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
    if (m_viewport)
        m_navigator->setVisibleRegion(m_viewport->visibleRegionNormalized());
}

void MainWindow::updateMetadata() {
    if (!m_metadataLabel) return;
    const RawMetadata& m = m_doc.currentRaw().metadata;
    const QString text = QStringLiteral("Camera: %1\nISO: %2\nAperture: f/%3\nShutter: %4s\n"
                                        "Focal: %5mm\nSize: %6 x %7")
                             .arg(m.cameraModel.isEmpty() ? QStringLiteral("-") : m.cameraModel)
                             .arg(m.iso)
                             .arg(m.aperture, 0, 'f', 1)
                             .arg(m.shutterSec, 0, 'g', 3)
                             .arg(m.focalLength, 0, 'f', 0)
                             .arg(m.width)
                             .arg(m.height);
    m_metadataLabel->setText(text);
}

void MainWindow::refreshPresetsList() {
    if (!m_presetsTree) return;
    m_presetsTree->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    for (const auto& p : m_doc.presetManager()->presets()) {
        const QString category = p.category.isEmpty() ? QStringLiteral("User Presets") : p.category;
        QTreeWidgetItem* parent = groups.value(category, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(m_presetsTree, {category});
            parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
            parent->setExpanded(true);
            groups.insert(category, parent);
        }
        auto* leaf = new QTreeWidgetItem(parent, {p.name});
        leaf->setData(0, Qt::UserRole, p.id);
    }
}

void MainWindow::refreshCatalogTrees() {
    if (m_foldersTree) {
        m_foldersTree->clear();
        for (const QString& path : m_doc.catalog()->folders()) {
            auto* item = new QTreeWidgetItem(m_foldersTree, {QDir(path).dirName().isEmpty()
                                                                 ? path
                                                                 : QDir(path).dirName()});
            item->setData(0, Qt::UserRole, path);
            item->setToolTip(0, path);
        }
    }
    if (m_collectionsTree) {
        m_collectionsTree->clear();
        for (const CollectionInfo& c : m_doc.catalog()->collections()) {
            auto* item = new QTreeWidgetItem(m_collectionsTree, {c.name});
            item->setData(0, Qt::UserRole, c.id);
        }
    }
}

void MainWindow::showImagePaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    m_doc.setImagePaths(paths);
    m_filmstrip->setImages(paths);
    m_libraryGrid->setImages(paths);
    refreshFilmstripThumbnails();
}

void MainWindow::refreshSnapshotsList() {
    if (!m_snapshotsList) return;
    m_snapshotsList->clear();
    for (const auto& s : m_doc.editGraph()->snapshots()) {
        auto* item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, s.id);
        m_snapshotsList->addItem(item);
    }
}

void MainWindow::refreshHistoryList() {
    if (!m_historyList) return;
    m_historyList->clear();
    const auto& labels = m_doc.editGraph()->historyLabels();
    for (int i = 0; i < labels.size(); ++i) {
        auto* item = new QListWidgetItem(labels[i]);
        if (i == m_doc.editGraph()->historyIndex()) item->setForeground(QColor(100, 160, 255));
        m_historyList->addItem(item);
    }
}

void MainWindow::refreshFilmstripThumbnails() {
    // Cancel any in-flight thumbnail job from a previous import.
    if (m_thumbWatcher.isRunning()) {
        m_thumbWatcher.cancel();
        m_thumbWatcher.waitForFinished();
    }

    m_thumbPaths = m_doc.imagePaths();
    if (m_thumbPaths.isEmpty()) return;

    // Decode thumbnails on the thread pool; results stream back to the UI thread
    // via the watcher's resultReadyAt signal, keeping the window responsive.
    DocumentController* doc = &m_doc;
    auto future = QtConcurrent::mapped(m_thumbPaths, [doc](const QString& path) -> QImage {
        return doc->thumbnailForPath(path);
    });
    m_thumbWatcher.setFuture(future);
}

} // namespace mylr
