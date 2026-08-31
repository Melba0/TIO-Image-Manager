#include "MainWindow.h"
#include "Logger.h"
#include "LanguageManager.h"
#include "ImageDetailsDialog.h"
#include "TagFilterDialog.h"
#include "ExportReportDialog.h"
#include "BatchEditDialog.h"
#include "managers/SettingsManager.h"
#include "managers/ModelManager.h"
#include "managers/ExtensionManager.h"
#include "managers/LibraryManager.h"
#include "managers/CollectionManager.h"
#include "managers/SmartCollectionManager.h"
#include "managers/ClusterNameMapping.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QGroupBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPixmap>
#include <QListWidgetItem>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QFontDatabase>
#include <QAction>
#include <QEvent>
#include <QRegularExpression>
#include <QInputDialog>
#include <QMenu>
#include <QToolBar>
#include <QSet>
#include <QMimeData>
#include <QPalette>
#include <QColor>
#include <algorithm>

QMimeData* ImageGrid::mimeData(const QList<QListWidgetItem*>& items) const {
    QStringList paths;
    for (QListWidgetItem* it : items) {
        const QString p = it->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    QMimeData* md = new QMimeData();
    md->setData("application/x-tio-images", paths.join('\n').toUtf8());
    md->setText(paths.join('\n'));
    return md;
}

void CollectionTree::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat("application/x-tio-images")) {
        e->acceptProposedAction();
    } else {
        QTreeWidget::dragEnterEvent(e);
    }
}

void CollectionTree::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat("application/x-tio-images")) {
        e->acceptProposedAction();
    } else {
        QTreeWidget::dragMoveEvent(e);
    }
}

void CollectionTree::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasFormat("application/x-tio-images")) {
        QTreeWidgetItem* target = itemAt(e->position().toPoint());
        if (target && target->data(0, Qt::UserRole).toString() == "normal") {
            const QString name = target->data(0, Qt::UserRole + 1).toString();
            if (!name.isEmpty()) {
                const QStringList paths =
                    QString::fromUtf8(e->mimeData()->data("application/x-tio-images"))
                        .split('\n', Qt::SkipEmptyParts);
                if (!paths.isEmpty()) emit imagesDropped(name, paths);
            }
            e->acceptProposedAction();
            return;
        }
        // Dropped on empty space / a branch header: do nothing.
        e->acceptProposedAction();
        return;
    }
    QTreeWidget::dropEvent(e);
}

static const char* kDarkQss = R"(
QMainWindow, QDialog { background-color: #1e1e1e; }
QMenuBar { background-color: #252526; color: #cccccc; }
QMenuBar::item:selected { background-color: #0e639c; }
QMenu { background-color: #252526; color: #cccccc; }
QMenu::item:selected { background-color: #0e639c; }
QGroupBox, QFrame { background-color: #1e1e1e; border: 1px solid #3e3e42; border-radius: 4px; margin-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; color: #cccccc; }
QLineEdit, QPlainTextEdit, QTextEdit {
    background-color: #2d2d30; color: #d4d4d4;
    border: 1px solid #3e3e42; border-radius: 3px; padding: 5px;
    font-family: Consolas, monospace; selection-background-color: #094771;
}
QPushButton {
    background-color: #0e639c; color: #ffffff; border: none;
    border-radius: 3px; padding: 6px 12px; font-weight: bold;
}
QPushButton:hover { background-color: #1177bb; }
QPushButton:pressed { background-color: #0a4b74; }
QPushButton:disabled { background-color: #3e3e42; color: #8a8a8a; }
QPushButton#searchBtn { background-color: #007acc; }
QPushButton#searchBtn:hover { background-color: #1a8cff; }
QLabel, QStatusBar { color: #cccccc; }
QWidget#searchBar { background-color: #2d2d30; padding: 8px; }
QListWidget, QStackedWidget, QTabWidget::pane {
    background-color: #1e1e1e; color: #d4d4d4;
    border: none; outline: none;
}
QListWidget::item { background-color: #1e1e1e; }
QListWidget::item:selected { background-color: #1e1e1e; }
QScrollBar:vertical { background: #2d2d30; width: 12px; }
QScrollBar::handle:vertical { background: #5a5a5e; border-radius: 6px; min-height: 24px; }
QScrollBar:horizontal { background: #2d2d30; height: 12px; }
QScrollBar::handle:horizontal { background: #5a5a5e; border-radius: 6px; min-width: 24px; }
QProgressBar { background-color: #2d2d30; border: 1px solid #3e3e42; border-radius: 3px; color: #cccccc; text-align: center; }
QProgressBar::chunk { background-color: #0e639c; }
QStatusBar { background-color: #252526; }
QWidget#resultItem { background-color: #252526; border-radius: 6px; padding: 4px; }
QLabel#scoreLabel { color: #4ec9b0; font-weight: bold; font-size: 14px; }
QLabel#nameLabel { color: #cccccc; font-size: 11px; }
QComboBox { background-color: #2d2d30; color: #d4d4d4; border: 1px solid #3e3e42; border-radius: 3px; padding: 3px 6px; }
QComboBox QAbstractItemView { background-color: #2d2d30; color: #d4d4d4; }
QListWidget#navList { background-color: #1e1e1e; color: #cccccc; }
QListWidget#navList::item { background-color: #1e1e1e; color: #cccccc; padding: 8px 6px; }
QListWidget#navList::item:hover { background-color: #2d2d30; }
QListWidget#navList::item:selected { background-color: #0e639c; color: #ffffff; }
QToolBar { background-color: #252526; border: none; spacing: 4px; }
QToolBar QToolButton { background: transparent; color: #cccccc; padding: 4px 8px; border-radius: 3px; }
QToolBar QToolButton:hover { background-color: #0e639c; color: #ffffff; }
QToolBar QToolButton:pressed { background-color: #0a4b74; }
QToolBar::separator { background: #3e3e42; width: 1px; margin: 4px 2px; }
QTreeWidget { background-color: #1e1e1e; color: #d4d4d4; border: none; outline: none; }
QTreeWidget::item { background-color: #1e1e1e; color: #d4d4d4; padding: 4px 2px; }
QTreeWidget::item:hover { background-color: #2d2d30; }
QTreeWidget::item:selected { background-color: #0e639c; color: #ffffff; }
QTreeWidget::branch { background-color: #1e1e1e; }
QSplitter::handle { background-color: #3e3e42; }
QSplitter::handle:horizontal { width: 2px; }
QTableWidget, QTableView { background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #3e3e42; gridline-color: #3e3e42; }
QTableWidget::item { color: #d4d4d4; padding: 2px; }
QTableWidget::item:selected { background-color: #0e639c; color: #ffffff; }
QHeaderView::section { background-color: #2d2d30; color: #d4d4d4; border: none; border-bottom: 1px solid #3e3e42; padding: 4px 6px; }
QScrollArea { background-color: #1e1e1e; border: none; }
QScrollArea > QWidget > QWidget { background-color: #1e1e1e; }
)";

static const char* kLightQss = R"(
QGroupBox, QFrame { border: 1px solid #c0c0c0; border-radius: 4px; }
QLineEdit, QPlainTextEdit, QTextEdit { border: 1px solid #c0c0c0; border-radius: 3px; padding: 5px; font-family: Consolas, monospace; }
QPushButton { background-color: #e8e8e8; border: 1px solid #b0b0b0; border-radius: 3px; padding: 6px 12px; }
QPushButton:hover { background-color: #d0d0d0; }
QListWidget#navList { background-color: #f3f3f3; color: #333333; }
QListWidget#navList::item { background-color: #f3f3f3; color: #333333; padding: 8px 6px; }
QListWidget#navList::item:hover { background-color: #e0e0e0; }
QListWidget#navList::item:selected { background-color: #0e639c; color: #ffffff; }
QToolBar { background-color: #f3f3f3; border: none; spacing: 4px; }
QToolBar QToolButton { background: transparent; color: #333333; padding: 4px 8px; border-radius: 3px; }
QToolBar QToolButton:hover { background-color: #0e639c; color: #ffffff; }
QTreeWidget { background-color: #f3f3f3; color: #333333; border: none; outline: none; }
QTreeWidget::item { background-color: #f3f3f3; color: #333333; padding: 4px 2px; }
QTreeWidget::item:hover { background-color: #e0e0e0; }
QTreeWidget::item:selected { background-color: #0e639c; color: #ffffff; }
QTreeWidget::branch { background-color: #f3f3f3; }
QSplitter::handle { background-color: #c0c0c0; }
QTableWidget, QTableView { background-color: #ffffff; color: #333333; border: 1px solid #c0c0c0; gridline-color: #d0d0d0; }
QTableWidget::item { color: #333333; padding: 2px; }
QTableWidget::item:selected { background-color: #0e639c; color: #ffffff; }
QHeaderView::section { background-color: #e8e8e8; color: #333333; border: none; border-bottom: 1px solid #c0c0c0; padding: 4px 6px; }
QScrollArea { background-color: #f3f3f3; border: none; }
QScrollArea > QWidget > QWidget { background-color: #f3f3f3; }
)";

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), llm_(new LlmClient(this)), engine_(new QProcess(this)) {
    buildUi();
    retranslateUi();   // build menus/actions (showDslAct_, showScoresAct_)
    applyTheme(SettingsManager::instance()->darkMode());
    LanguageManager::instance()->language();  // ensure translator state

    connect(engine_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onEngineFinished);
    connect(engine_, &QProcess::readyReadStandardError, this, &MainWindow::onEngineStdErr);

    thumbTimer_ = new QTimer(this);
    thumbTimer_->setInterval(12);
    connect(thumbTimer_, &QTimer::timeout, this, &MainWindow::onThumbnailTick);

    connect(llm_, &LlmClient::dslReady, this, &MainWindow::onLlmResult);
    connect(llm_, &LlmClient::requestFailed, this, &MainWindow::onLlmError);

    connect(settingsPage_, &SettingsPage::settingsChanged, this, [this]() {
        refreshStats();
        refreshStatusBar();
    });
    connect(LibraryManager::instance(), &LibraryManager::countReady, this,
            [this](qint64) { refreshStats(); refreshStatusBar(); });
    connect(langBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onLanguageChanged);
    LibraryManager::instance()->rescanAsync();

    // ---- collection panels ----
    connect(collectionTree_, &QTreeWidget::itemClicked, this, &MainWindow::onCollectionItemClicked);
    connect(collectionTree_, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onCollectionItemDoubleClicked);
    connect(collectionTree_, &QWidget::customContextMenuRequested, this, &MainWindow::onCollectionContextMenu);
    connect(collectionTree_, &CollectionTree::imagesDropped, this, &MainWindow::onImagesDropped);
    connect(CollectionManager::instance(), &CollectionManager::collectionsChanged,
            this, &MainWindow::reloadCollectionPanel);
    connect(SmartCollectionManager::instance(), &SmartCollectionManager::smartCollectionsChanged,
            this, &MainWindow::reloadCollectionPanel);
    connect(saveSmartAct_, &QAction::triggered, this, &MainWindow::onSaveSmartCollection);
    connect(exportAct_, &QAction::triggered, this, &MainWindow::onExportReport);
    connect(batchAct_, &QAction::triggered, this, &MainWindow::onBatchEdit);
    reloadCollectionPanel();

    // apply saved View settings
    SettingsManager* s = SettingsManager::instance();
    showDslAct_->setChecked(s->value("view/show_dsl", true).toBool());
    showScoresAct_->setChecked(s->value("view/show_scores", true).toBool());
    showScores_ = showScoresAct_->isChecked();
    dslBox_->setVisible(showDslAct_->isChecked());

    loadSavedTagFilters();
    if (!tagFilters_.isEmpty()) {
        statusBar()->showMessage(tr("Tag filter active: %1").arg(tagFilterSummary()));
    }

    refreshStats();
    refreshStatusBar();
    qInfo() << "Application started";
    QTimer::singleShot(200, this, &MainWindow::startWarmup);
}

MainWindow::~MainWindow() {
    if (engine_->state() != QProcess::NotRunning) {
        engine_->kill();
        engine_->waitForFinished(2000);
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi() {
    setWindowTitle(tr("Image Retrieval DSL Tool"));
    menuBar()->clear();
    // rebuild menus (simplest reliable retranslation)
    QMenu* fileMenu = menuBar()->addMenu(tr("File"));
    QAction* reindexAct = fileMenu->addAction(tr("Reindex Cache"));
    fileMenu->addSeparator();
    QAction* exitAct = fileMenu->addAction(tr("Exit"));
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    connect(reindexAct, &QAction::triggered, this, &MainWindow::reindexCache);

    QMenu* settingsMenu = menuBar()->addMenu(tr("Settings"));
    const QStringList settingTabs = {
        tr("General"), tr("API Config"), tr("Library"),
        tr("Models"), tr("Inference"), tr("Extensions"), tr("Logs")};
    for (int i = 0; i < settingTabs.size(); ++i) {
        QAction* act = settingsMenu->addAction(settingTabs[i]);
        connect(act, &QAction::triggered, this, [this, i]() { gotoSettings(i); });
    }

    QMenu* viewMenu = menuBar()->addMenu(tr("View"));
    showDslAct_ = viewMenu->addAction(tr("Show DSL Editor"));
    showDslAct_->setCheckable(true);
    showScoresAct_ = viewMenu->addAction(tr("Show Scores"));
    showScoresAct_->setCheckable(true);
    QAction* refreshAct = viewMenu->addAction(tr("Refresh"));
    QAction* resetAct = viewMenu->addAction(tr("Reset Layout"));
    connect(showDslAct_, &QAction::toggled, this, [this](bool on) {
        dslBox_->setVisible(on);
        SettingsManager::instance()->setValue("view/show_dsl", on);
    });
    connect(showScoresAct_, &QAction::toggled, this, [this](bool on) {
        showScores_ = on;
        SettingsManager::instance()->setValue("view/show_scores", on);
        // update already-created items
        for (int i = 0; i < resultGrid_->count(); ++i) {
            if (auto* w = qobject_cast<ResultItemWidget*>(resultGrid_->itemWidget(resultGrid_->item(i))))
                w->setShowScore(on);
        }
    });
    connect(refreshAct, &QAction::triggered, this, &MainWindow::refreshView);
    connect(resetAct, &QAction::triggered, this, [this]() {
        resize(1120, 720);
        stack_->setCurrentWidget(searchPage_);
    });

    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
    QAction* aboutAct = helpMenu->addAction(tr("About"));
    QAction* updateAct = helpMenu->addAction(tr("Check for Updates"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
    connect(updateAct, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("Check for Updates"), tr("You are up to date."));
    });
    exitAct->setShortcut(QKeySequence::Quit);

    searchEdit_->setPlaceholderText(
        tr("Enter a natural-language description, e.g. a cat and a dog, cat on the left..."));
    translateBtn_->setText(tr("Translate to DSL (→)"));
    execBtn_->setText(tr("Search (▶)"));
    tagFilterBtn_->setText(tr("🏷️ Tag Filter"));
    deleteBtn_->setText(tr("🗑 Delete Selected"));
    deleteBtn_->setToolTip(tr("Delete the selected images (disk files + cache)"));
    dslBox_->setTitle(tr("DSL Code (editable)"));
    dslEdit_->setPlaceholderText(tr("Generated DSL appears here; edit and click Search."));

    if (saveSmartAct_) saveSmartAct_->setText("⭐ " + tr("Save as Smart Album"));
    if (exportAct_) exportAct_->setText("📄 " + tr("Export Report"));
    if (batchAct_) batchAct_->setText("✏️ " + tr("Batch Edit"));
    if (collectionTitleLabel_) collectionTitleLabel_->setText("📁 " + tr("Albums"));
    refreshStats();
    refreshStatusBar();
}

void MainWindow::buildUi() {
    searchPage_ = new QWidget(this);
    QVBoxLayout* sPageLay = new QVBoxLayout(searchPage_);
    sPageLay->setContentsMargins(0, 0, 0, 0);
    sPageLay->setSpacing(0);

    // search bar (60px)
    QWidget* searchBar = new QWidget(searchPage_);
    searchBar->setObjectName("searchBar");
    searchBar->setFixedHeight(60);
    QHBoxLayout* sLayout = new QHBoxLayout(searchBar);
    sLayout->setContentsMargins(8, 8, 8, 8);
    searchEdit_ = new QLineEdit(searchBar);
    translateBtn_ = new QPushButton(searchBar);
    execBtn_ = new QPushButton(searchBar);
    execBtn_->setObjectName("searchBtn");
    spinner_ = new Spinner(searchBar);
    langBox_ = new QComboBox(searchBar);
    langBox_->addItems({"EN", "中文"});
    langBox_->setFixedWidth(72);
    if (LanguageManager::instance()->language() == "zh") langBox_->setCurrentIndex(1);
    tagFilterBtn_ = new QPushButton(tr("🏷️ Tag Filter"), searchBar);
    deleteBtn_ = new QPushButton(tr("🗑 Delete Selected"), searchBar);
    deleteBtn_->setToolTip(tr("Delete the selected images (disk files + cache)"));
    sLayout->addWidget(searchEdit_, 1);
    sLayout->addWidget(translateBtn_);
    sLayout->addWidget(execBtn_);
    sLayout->addWidget(spinner_);
    sLayout->addWidget(tagFilterBtn_);
    sLayout->addWidget(deleteBtn_);
    sLayout->addWidget(langBox_);
    connect(tagFilterBtn_, &QPushButton::clicked, this, &MainWindow::onTagFilterClicked);
    connect(deleteBtn_, &QPushButton::clicked, this, &MainWindow::onDeleteSelected);
    sPageLay->addWidget(searchBar);

    // collapsible DSL editor (initial ~120px)
    dslBox_ = new QGroupBox(searchPage_);
    QVBoxLayout* dslLay = new QVBoxLayout(dslBox_);
    dslEdit_ = new QPlainTextEdit(dslBox_);
    dslEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dslEdit_->setMaximumHeight(160);
    dslLay->addWidget(dslEdit_);
    dslBox_->setMaximumHeight(190);
    sPageLay->addWidget(dslBox_);

    // results grid
    resultGrid_ = new ImageGrid(searchPage_);
    resultGrid_->setViewMode(QListView::IconMode);
    resultGrid_->setResizeMode(QListView::Adjust);
    resultGrid_->setSelectionMode(QAbstractItemView::ExtendedSelection);  // multi-select
    resultGrid_->setSpacing(10);
    resultGrid_->setIconSize(QSize(120, 120));
    resultGrid_->setGridSize(QSize(160, 170));
    resultGrid_->setWordWrap(false);
    resultGrid_->setMovement(QListView::Static);
    resultGrid_->setDragEnabled(true);       // drag images onto albums
    resultGrid_->setAcceptDrops(false);
    resultGrid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(resultGrid_, &QListWidget::itemDoubleClicked, this, &MainWindow::onResultDoubleClicked);
    connect(resultGrid_, &QWidget::customContextMenuRequested, this, &MainWindow::onGridContextMenu);
    sPageLay->addWidget(resultGrid_, 1);

    // Construct LanguageManager before building the settings page so its panels
    // are created with the correct translator already installed.
    LanguageManager::instance();
    settingsPage_ = new SettingsPage(this);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(searchPage_);
    stack_->addWidget(settingsPage_);

    // stats badges
    QWidget* statsBar = new QWidget(this);
    QHBoxLayout* statsLayout = new QHBoxLayout(statsBar);
    statsLayout->setContentsMargins(8, 4, 8, 2);
    statLibrary_ = createStatLabel("📁");
    statModel_ = createStatLabel("🧠 -");
    statExt_ = createStatLabel("🔌");
    statCache_ = createStatLabel("⚡");
    statsLayout->addWidget(statLibrary_);
    statsLayout->addWidget(statModel_);
    statsLayout->addWidget(statExt_);
    statsLayout->addWidget(statCache_);
    statsLayout->addStretch();

    // left: collection panel; right: stats + stacked pages
    leftPanel_ = buildCollectionPanel();
    QWidget* rightColumn = new QWidget(this);
    QVBoxLayout* rightLay = new QVBoxLayout(rightColumn);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addWidget(statsBar);
    rightLay->addWidget(stack_, 1);

    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->addWidget(leftPanel_);
    mainSplitter_->addWidget(rightColumn);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setSizes({230, 900});
    mainSplitter_->setCollapsible(0, false);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);
    mainLay->addWidget(mainSplitter_, 1);
    setCentralWidget(central);

    // ---- top toolbar: report / export / batch ----
    QToolBar* toolbar = addToolBar(tr("Actions"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    saveSmartAct_ = toolbar->addAction("⭐ " + tr("Save as Smart Album"));
    exportAct_ = toolbar->addAction("📄 " + tr("Export Report"));
    batchAct_ = toolbar->addAction("✏️ " + tr("Batch Edit"));

    statusBar()->addWidget(new QLabel(" ", this), 1);
    statusBar()->showMessage(tr("Ready"));
    resize(1200, 760);

    connect(translateBtn_, &QPushButton::clicked, this, &MainWindow::onTranslateClicked);
    connect(execBtn_, &QPushButton::clicked, this, &MainWindow::onExecuteClicked);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &MainWindow::onTranslateClicked);
}

QWidget* MainWindow::buildCollectionPanel() {
    QWidget* panel = new QWidget(this);
    panel->setMinimumWidth(180);
    QVBoxLayout* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(4);

    QHBoxLayout* head = new QHBoxLayout();
    collectionTitleLabel_ = new QLabel("📁 " + tr("Albums"), panel);
    collectionTitleLabel_->setStyleSheet("font-weight:bold;");
    QPushButton* newBtn = new QPushButton("＋", panel);
    newBtn->setToolTip(tr("New album"));
    newBtn->setFixedWidth(26);
    head->addWidget(collectionTitleLabel_);
    head->addStretch();
    head->addWidget(newBtn);
    vl->addLayout(head);

    collectionTree_ = new CollectionTree(panel);
    collectionTree_->setHeaderHidden(true);
    collectionTree_->setIndentation(16);
    collectionTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    collectionTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    collectionTree_->setAcceptDrops(true);
    collectionTree_->setDragEnabled(false);
    vl->addWidget(collectionTree_, 1);

    connect(newBtn, &QPushButton::clicked, this, &MainWindow::onNewCollection);
    return panel;
}

// Rebuild the left panel's tree: two branches (normal albums + smart albums).
void MainWindow::reloadCollectionPanel() {
    collectionTree_->blockSignals(true);
    collectionTree_->clear();

    QTreeWidgetItem* albumBranch = new QTreeWidgetItem(collectionTree_);
    albumBranch->setText(0, "📁 " + tr("Albums"));
    albumBranch->setData(0, Qt::UserRole, "branch");
    QTreeWidgetItem* smartBranch = new QTreeWidgetItem(collectionTree_);
    smartBranch->setText(0, "🔍 " + tr("Smart Albums"));
    smartBranch->setData(0, Qt::UserRole, "branch");

    const QStringList names = CollectionManager::instance()->names();
    for (const QString& n : names) {
        QTreeWidgetItem* it = new QTreeWidgetItem(albumBranch);
        it->setText(0, "📁 " + n);
        it->setData(0, Qt::UserRole, "normal");
        it->setData(0, Qt::UserRole + 1, n);
        it->setToolTip(0, tr("%1 images").arg(CollectionManager::instance()->imagesIn(n).size()));
    }
    const QStringList smart = SmartCollectionManager::instance()->names();
    for (const QString& n : smart) {
        QTreeWidgetItem* it = new QTreeWidgetItem(smartBranch);
        it->setText(0, "🔍 " + n);
        it->setData(0, Qt::UserRole, "smart");
        it->setData(0, Qt::UserRole + 1, n);
    }
    albumBranch->setExpanded(true);
    smartBranch->setExpanded(true);

    // ---- clustering packs (V2): one branch per active pack with data ----
    ExtensionManager::instance()->scan();
    const QList<ExtPack> packs = ExtensionManager::instance()->packs();
    for (const ExtPack& p : packs) {
        if (!p.active || !p.canCluster || !p.showInSidebar || p.clusterName.isEmpty()) continue;
        QHash<QString, QStringList> imgMap = CollectionManager::clusterImages(p.clusterName);
        if (imgMap.isEmpty()) continue;

        QString title = p.icon.isEmpty() ? p.groupLabel : p.icon + " " + p.groupLabel;
        QTreeWidgetItem* clusterBranch = new QTreeWidgetItem(collectionTree_);
        clusterBranch->setText(0, title);
        clusterBranch->setData(0, Qt::UserRole, "branch");

        // Sort clusters by image count (descending), then by id.
        QList<QPair<int, QString>> order;
        for (auto it = imgMap.begin(); it != imgMap.end(); ++it) order.append({it.value().size(), it.key()});
        std::sort(order.begin(), order.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        for (const auto& pr : order) {
            const QString clusterId = pr.second;
            QString display = ClusterNameMapping::instance()->displayName(clusterId, p.clusterName);
            QTreeWidgetItem* it = new QTreeWidgetItem(clusterBranch);
            it->setText(0, QString("%1 (%2)").arg(display).arg(pr.first));
            it->setData(0, Qt::UserRole, "cluster");
            it->setData(0, Qt::UserRole + 1, clusterId);
            it->setData(0, Qt::UserRole + 2, p.clusterName);
        }
        clusterBranch->setExpanded(true);
    }

    collectionTree_->blockSignals(false);
}

QStringList MainWindow::selectedGridPaths() const {
    QStringList paths;
    for (QListWidgetItem* it : resultGrid_->selectedItems()) {
        const QString p = it->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    return paths;
}

void MainWindow::openCollection(const QString& name) {
    currentCollectionName_ = name;
    QString escaped = name;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    executeDsl(QString("collection(\"%1\")").arg(escaped));
}

void MainWindow::onCollectionItemClicked(QTreeWidgetItem* item, int) {
    if (!item) return;
    const QString type = item->data(0, Qt::UserRole).toString();
    if (type == "normal") {
        openCollection(item->data(0, Qt::UserRole + 1).toString());
    } else if (type == "cluster") {
        showClusterImages(item->data(0, Qt::UserRole + 2).toString(),
                          item->data(0, Qt::UserRole + 1).toString());
    }
}

void MainWindow::onCollectionItemDoubleClicked(QTreeWidgetItem* item, int) {
    if (!item) return;
    const QString type = item->data(0, Qt::UserRole).toString();
    if (type == "smart") {
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        currentCollectionName_.clear();   // a smart-album run is not a collection browse
        executeDsl(SmartCollectionManager::instance()->dslFor(name));
    } else if (type == "cluster") {
        onRenameCluster(item);
    }
}

void MainWindow::onRenameCluster(QTreeWidgetItem* item) {
    if (!item) return;
    const QString clusterId = item->data(0, Qt::UserRole + 1).toString();
    const QString clusterName = item->data(0, Qt::UserRole + 2).toString();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename Group"),
                                         tr("Display name for this group:"), QLineEdit::Normal,
                                         ClusterNameMapping::instance()->displayName(clusterId, clusterName), &ok);
    if (!ok) return;
    ClusterNameMapping::instance()->setMapping(clusterId, name.trimmed());
    reloadCollectionPanel();
}

// Show every photo whose cluster_groups[clusterName] contains clusterId by
// synthesizing the same result JSON the engine emits (reuses the grid pipeline).
void MainWindow::showClusterImages(const QString& clusterName, const QString& clusterId) {
    currentCollectionName_.clear();
    const QHash<QString, QStringList> imgMap = CollectionManager::clusterImages(clusterName);
    const QStringList paths = imgMap.value(clusterId);

    QJsonObject obj;
    obj["type"] = "images";
    const QStringList dirs = LibraryManager::instance()->paths();
    obj["photo_dir"] = dirs.isEmpty() ? QString() : dirs.first();
    QJsonArray roots;
    for (const QString& d : dirs) {
        roots.append(QJsonObject{{"prefix", QDir(d).dirName()}, {"dir", d}});
    }
    obj["photo_roots"] = roots;
    QJsonArray results;
    for (const QString& p : paths) {
        results.append(QJsonObject{{"path", p}, {"score", 1.0}});
    }
    obj["results"] = results;
    showResults(obj);
    statusBar()->showMessage(tr("Group \"%1\": %2 photo(s)")
                                 .arg(ClusterNameMapping::instance()->displayName(clusterId, clusterName))
                                 .arg(paths.size()));
}

void MainWindow::onCollectionContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = collectionTree_->itemAt(pos);
    const QString type = item ? item->data(0, Qt::UserRole).toString() : QString();

    QMenu menu(this);
    QAction* newAct = menu.addAction(tr("New Album..."));
    QAction* renameAct = nullptr;
    QAction* delAct = nullptr;
    if (type == "normal" || type == "smart") {
        renameAct = menu.addAction(tr("Rename..."));
        delAct = menu.addAction(tr("Delete"));
    } else if (type == "cluster") {
        renameAct = menu.addAction(tr("Rename..."));
    }
    QAction* chosen = menu.exec(collectionTree_->viewport()->mapToGlobal(pos));
    if (chosen == newAct) {
        onNewCollection();
    } else if (chosen == renameAct) {
        if (type == "cluster") onRenameCluster(item);
        else onRenameCollection(item);
    } else if (chosen == delAct) {
        onDeleteCollection(item);
    }
}

void MainWindow::onNewCollection() {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Album"), tr("Album name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty()) return;
    if (!CollectionManager::instance()->create(name)) {
        statusBar()->showMessage(tr("Album \"%1\" already exists or could not be created.").arg(name));
        return;
    }
    statusBar()->showMessage(tr("Created album \"%1\".").arg(name));
    reloadCollectionPanel();
}

void MainWindow::onRenameCollection(QTreeWidgetItem* item) {
    if (!item) return;
    const QString type = item->data(0, Qt::UserRole).toString();
    const QString oldName = item->data(0, Qt::UserRole + 1).toString();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                         QLineEdit::Normal, oldName, &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty() || name == oldName) return;
    bool renamed = (type == "smart")
                       ? SmartCollectionManager::instance()->rename(oldName, name)
                       : CollectionManager::instance()->rename(oldName, name);
    if (!renamed) {
        statusBar()->showMessage(tr("Rename failed (name may already be in use)."));
    } else {
        if (type == "normal" && currentCollectionName_ == oldName) currentCollectionName_ = name;
        statusBar()->showMessage(tr("Renamed to \"%1\".").arg(name));
    }
    reloadCollectionPanel();
}

void MainWindow::onDeleteCollection(QTreeWidgetItem* item) {
    if (!item) return;
    const QString type = item->data(0, Qt::UserRole).toString();
    const QString name = item->data(0, Qt::UserRole + 1).toString();
    auto r = QMessageBox::warning(this, tr("Delete Album"),
                                  tr("Delete album \"%1\"? (images on disk are kept)").arg(name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;
    bool removed = (type == "smart")
                       ? SmartCollectionManager::instance()->remove(name)
                       : CollectionManager::instance()->remove(name);
    if (removed && currentCollectionName_ == name) currentCollectionName_.clear();
    reloadCollectionPanel();
}

void MainWindow::onAddToCollection(const QString& name) {
    QStringList paths = selectedGridPaths();
    if (paths.isEmpty()) {
        if (QListWidgetItem* it = resultGrid_->currentItem())
            paths << it->data(Qt::UserRole).toString();
    }
    if (paths.isEmpty()) {
        statusBar()->showMessage(tr("No image selected."));
        return;
    }
    if (CollectionManager::instance()->addImages(name, paths)) {
        statusBar()->showMessage(tr("Added %1 image(s) to \"%2\".").arg(paths.size()).arg(name));
        reloadCollectionPanel();
    }
}

void MainWindow::onRemoveFromCollection() {
    if (currentCollectionName_.isEmpty()) return;
    const QStringList paths = selectedGridPaths();
    if (paths.isEmpty()) {
        statusBar()->showMessage(tr("No image selected."));
        return;
    }
    int n = 0;
    for (const QString& p : paths) {
        if (CollectionManager::instance()->removeImage(currentCollectionName_, p)) ++n;
    }
    statusBar()->showMessage(tr("Removed %1 image(s) from \"%2\".").arg(n).arg(currentCollectionName_));
    reloadCollectionPanel();
}

void MainWindow::onImagesDropped(const QString& collection, const QStringList& paths) {
    if (CollectionManager::instance()->addImages(collection, paths)) {
        statusBar()->showMessage(tr("Added %1 image(s) to \"%2\".").arg(paths.size()).arg(collection));
        reloadCollectionPanel();
    }
}

void MainWindow::onSaveSmartCollection() {
    const QString dsl = dslEdit_->toPlainText().trimmed();
    if (dsl.isEmpty()) {
        statusBar()->showMessage(tr("DSL is empty - nothing to save."));
        return;
    }
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save as Smart Album"),
                                         tr("Smart album name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty()) return;
    if (SmartCollectionManager::instance()->save(name, dsl)) {
        statusBar()->showMessage(tr("Saved smart album \"%1\".").arg(name));
        reloadCollectionPanel();
    }
}

void MainWindow::onExportReport() {
    QVector<ExportImage> all;
    QSet<QString> selectedPaths;
    for (QListWidgetItem* it : resultGrid_->selectedItems()) {
        selectedPaths.insert(it->data(Qt::UserRole).toString());
    }
    for (const ResultItem& it : pending_) {
        if (it.score < 0.05) continue;   // only exported items are the visible ones
        all.append({it.path, it.fullPath, it.score});
    }
    QVector<ExportImage> selected;
    for (const ResultItem& it : pending_) {
        if (it.score < 0.05) continue;
        if (selectedPaths.contains(it.path)) selected.append({it.path, it.fullPath, it.score});
    }
    ExportReportDialog dlg(all, selected, this);
    dlg.exec();
}

void MainWindow::onBatchEdit() {
    QSet<QString> selectedPaths;
    for (QListWidgetItem* it : resultGrid_->selectedItems()) {
        selectedPaths.insert(it->data(Qt::UserRole).toString());
    }
    if (selectedPaths.isEmpty()) {
        statusBar()->showMessage(tr("No image selected."));
        return;
    }
    QVector<BatchImage> images;
    for (const ResultItem& it : pending_) {
        if (it.score < 0.05) continue;
        if (selectedPaths.contains(it.path)) images.append({it.path, it.fullPath, it.score});
    }
    if (images.isEmpty()) return;
    BatchEditDialog dlg(images, this);
    dlg.exec();
    if (dlg.changed()) {
        // Re-run the current query so the grid reflects renames / tag edits.
        if (!currentCollectionName_.isEmpty()) {
            openCollection(currentCollectionName_);
        } else if (!dslEdit_->toPlainText().trimmed().isEmpty()) {
            executeDsl(dslEdit_->toPlainText());
        }
    }
}

void MainWindow::onGridContextMenu(const QPoint& pos) {
    QListWidgetItem* item = resultGrid_->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QMenu* addSub = menu.addMenu(tr("Add to Album"));
    const QStringList names = CollectionManager::instance()->names();
    if (names.isEmpty()) {
        QAction* none = addSub->addAction(tr("(no albums - create one)"));
        none->setEnabled(false);
    }
    for (const QString& n : names) {
        QAction* act = addSub->addAction("📁 " + n);
        connect(act, &QAction::triggered, this, [this, n]() { onAddToCollection(n); });
    }
    if (!currentCollectionName_.isEmpty()) {
        QAction* rem = menu.addAction(tr("Remove from Album"));
        connect(rem, &QAction::triggered, this, &MainWindow::onRemoveFromCollection);
    }
    menu.addSeparator();
    QAction* details = menu.addAction(tr("Details"));
    connect(details, &QAction::triggered, this, [this, item]() { onResultDoubleClicked(item); });
    menu.exec(resultGrid_->viewport()->mapToGlobal(pos));
}

QLabel* MainWindow::createStatLabel(const QString& text) {
    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet(
        "background: #2d2d30; color: #d4d4d4; padding: 2px 10px; border-radius: 10px; border: 1px solid #3e3e42;");
    return lbl;
}

void MainWindow::applyTheme(bool dark) {
    // Set a matching QPalette so every widget NOT explicitly styled by the QSS
    // (tree view, tables, toolbar buttons, dialog bits, scrollbars...) follows
    // the theme too: dark mode -> light text, light mode -> dark text.
    QPalette pal;
    if (dark) {
        pal.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1e));
        pal.setColor(QPalette::WindowText, QColor(0xcc, 0xcc, 0xcc));
        pal.setColor(QPalette::Base, QColor(0x2d, 0x2d, 0x30));
        pal.setColor(QPalette::AlternateBase, QColor(0x25, 0x25, 0x26));
        pal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
        pal.setColor(QPalette::Button, QColor(0x2d, 0x2d, 0x30));
        pal.setColor(QPalette::ButtonText, QColor(0xcc, 0xcc, 0xcc));
        pal.setColor(QPalette::BrightText, Qt::red);
        pal.setColor(QPalette::Link, QColor(0x4e, 0xc9, 0xb0));
        pal.setColor(QPalette::Highlight, QColor(0x0e, 0x63, 0x9c));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
        pal.setColor(QPalette::ToolTipBase, QColor(0x25, 0x25, 0x26));
        pal.setColor(QPalette::ToolTipText, QColor(0xcc, 0xcc, 0xcc));
    } else {
        pal.setColor(QPalette::Window, QColor(0xf3, 0xf3, 0xf3));
        pal.setColor(QPalette::WindowText, QColor(0x33, 0x33, 0x33));
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf0, 0xf0));
        pal.setColor(QPalette::Text, QColor(0x33, 0x33, 0x33));
        pal.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
        pal.setColor(QPalette::ButtonText, QColor(0x33, 0x33, 0x33));
        pal.setColor(QPalette::Link, QColor(0x0e, 0x63, 0x9c));
        pal.setColor(QPalette::Highlight, QColor(0x0e, 0x63, 0x9c));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::ToolTipBase, Qt::white);
        pal.setColor(QPalette::ToolTipText, QColor(0x33, 0x33, 0x33));
    }
    qApp->setPalette(pal);
    qApp->setStyleSheet(QString::fromUtf8(dark ? kDarkQss : kLightQss));
    SettingsManager::instance()->setDarkMode(dark);
}

void MainWindow::onLanguageChanged(int index) {
    LanguageManager::instance()->setLanguage(index == 1 ? "zh" : "en");
}

void MainWindow::gotoSettings(int tab) {
    stack_->setCurrentWidget(settingsPage_);
    settingsPage_->setCurrentTab(tab);
}

void MainWindow::refreshView() {
    ModelManager::instance()->scan();
    ExtensionManager::instance()->scan();
    LibraryManager::instance()->rescanAsync();
    refreshStats();
    refreshStatusBar();
}

void MainWindow::reindexCache() {
    QString active = ModelManager::instance()->activeModel();
    QString cacheDir = SettingsManager::projectDir() + "/cache/" + active;
    QDir d(cacheDir);
    if (d.exists() && d.removeRecursively()) {
        statusBar()->showMessage(tr("Cleared cache for %1; next search rebuilds.").arg(active));
        qInfo() << "Reindex cache: " << cacheDir;
    } else {
        statusBar()->showMessage(tr("Cache is already up to date."));
    }
}

void MainWindow::toggleDarkMode() {
    applyTheme(!SettingsManager::instance()->darkMode());
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About"),
                       tr("Image Retrieval DSL Tool v1.0\n\nFuzzy-probability DSL engine + LLM natural-language-to-DSL."));
}

void MainWindow::refreshStats() {
    ModelManager::instance()->scan();
    QString model = ModelManager::instance()->activeModel();
    int ext = 0;
    for (const ExtPack& p : ExtensionManager::instance()->packs()) {
        if (p.active) ++ext;
    }
    statLibrary_->setText(tr("📁 Library: %1").arg(LibraryManager::instance()->countImages()));
    statModel_->setText(tr("🧠 %1").arg(model));
    statExt_->setText(tr("🔌 Extensions: %1").arg(ext));
    statCache_->setText(tr("⚡ Cache hit: %1%")
                            .arg(runs_ > 0 ? QString::number(cacheHits_ * 100 / runs_) : QString("-")));
}

void MainWindow::refreshStatusBar() {
    ModelManager::instance()->scan();
    QString model = ModelManager::instance()->activeModel();
    int ext = 0;
    for (const ExtPack& p : ExtensionManager::instance()->packs()) {
        if (p.active) ++ext;
    }
    statusBar()->showMessage(tr("Model: [%1]    Extensions: [%2]    Library: %3 images")
                                 .arg(model).arg(ext).arg(LibraryManager::instance()->countImages()));
}

QString MainWindow::buildClassesSummary() const {
    // Feed the LLM the ACTIVE model's classes.json verbatim so it generates
    // DSL with the exact class names (including underscore names like
    // "traffic_light" and parent classes like "fruit"/"vehicle").
    QString proj = SettingsManager::projectDir();
    QString active;
    {
        QFile f(proj + "/models/registry.json");
        if (f.open(QIODevice::ReadOnly)) {
            active = QJsonDocument::fromJson(f.readAll()).object()["active_base"].toString();
        }
    }
    if (active.isEmpty()) return QString();
    QFile f(proj + "/models/base/" + active + "/classes.json");
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

QString MainWindow::buildExtensionsSummary() const {
    QStringList lines;
    for (const ExtPack& p : ExtensionManager::instance()->packs()) {
        QStringList q;
        for (const QString& c : p.children) q << "\"" + c + "\"";
        lines << QString(">> %1 (expands \"%2\" to [%3])").arg(p.name, p.parentClass, q.join(", "));
    }
    return lines.join("\n");
}

QStringList MainWindow::engineArgs() const {
    QStringList args;
    args << "--json";
    for (const auto& p : LibraryManager::instance()->paths()) {
        args << "--photo" << p;
    }
    // active tag pre-filter (key=v1|v2, values OR-ed)
    for (const auto& f : tagFilters_) {
        args << "--tag-filter" << (f.first + "=" + f.second.join('|'));
    }
    return args;
}

// Build/refresh the inference cache in the background right after startup so
// the first real query does not pay the full preprocessing cost.  Progress
// messages stream to the log panel via onEngineStdErr.
void MainWindow::startWarmup() {
    if (engine_->state() != QProcess::NotRunning) return;
    if (!QFile::exists(SettingsManager::instance()->enginePath())) return;
    warmupRunning_ = true;
    engineErrBuf_.clear();
    engine_->start(SettingsManager::instance()->enginePath(), engineArgs() << "--warmup");
    if (engine_->waitForStarted(5000)) {
        engine_->closeWriteChannel();
        qInfo() << "[Preprocess] engine --warmup started";
    } else {
        warmupRunning_ = false;
    }
}

void MainWindow::onTranslateClicked() {
    QString input = searchEdit_->text().trimmed();
    if (input.isEmpty()) {
        statusBar()->showMessage(tr("Please enter a description first"));
        return;
    }
    SettingsManager* s = SettingsManager::instance();
    if (s->apiKey().isEmpty()) {
        statusBar()->showMessage(tr("API key not set - open Settings -> API Config"));
        gotoSettings(1);
        return;
    }
    spinner_->start();
    translateBtn_->setEnabled(false);
    statusBar()->showMessage(tr("Requesting LLM..."));
    qInfo() << "LLM translate request: " << input;
    llm_->translateToDsl(input, buildClassesSummary(), buildExtensionsSummary(),
                         s->baseUrl(), s->apiKey(), s->modelName());
}

void MainWindow::onLlmResult(const QString& dsl) {
    spinner_->stop();
    translateBtn_->setEnabled(true);
    dslEdit_->setPlainText(dsl);
    dslBox_->setVisible(showDslAct_->isChecked());
    statusBar()->showMessage(tr("LLM generated DSL - click Search to run"));
}

void MainWindow::onLlmError(const QString& msg) {
    spinner_->stop();
    translateBtn_->setEnabled(true);
    statusBar()->showMessage(tr("LLM request failed: %1").arg(msg));
    qWarning() << "LLM request failed: " << msg;
}

void MainWindow::onExecuteClicked() {
    const QString dsl = dslEdit_->toPlainText();
    if (dsl.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("DSL is empty - translate or type it first"));
        return;
    }
    currentCollectionName_.clear();   // a manual search is not a collection browse
    executeDsl(dsl);
}

// Launch the engine with the given DSL and stream results into the grid.  Used
// by the Search button, album browsing and smart-collection execution alike.
void MainWindow::executeDsl(const QString& dsl) {
    // Safety: the DSL may contain a `del` statement that permanently deletes
    // image files — confirm with the user first.
    QRegularExpression delRe(R"(\bdel\b)");
    if (dsl.contains(delRe)) {
        auto r = QMessageBox::warning(
            this, tr("Delete Confirmation"),
            tr("DSL 包含删除指令 (del)，将永久删除图片文件并更新缓存。\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }
    QString enginePath = SettingsManager::instance()->enginePath();
    if (!QFile::exists(enginePath)) {
        statusBar()->showMessage(tr("Engine not found: %1").arg(enginePath));
        return;
    }
    if (engine_->state() != QProcess::NotRunning) {
        // A startup warmup may still be building the cache; abort it and run
        // the user's query right away (the warmup was only a convenience).
        if (warmupRunning_) {
            qInfo() << "[Preprocess] aborting warmup to serve the query";
            engine_->kill();
            engine_->waitForFinished(3000);
            warmupRunning_ = false;
        } else {
            statusBar()->showMessage(tr("Engine is already running"));
            return;
        }
    }

    dslEdit_->setPlainText(dsl);   // reflect what is being executed
    resultGrid_->clear();
    pending_.clear();
    thumbPos_ = 0;
    filteredLow_ = 0;
    thumbTimer_->stop();
    engineErrBuf_.clear();
    spinner_->start();
    execBtn_->setEnabled(false);
    statusBar()->showMessage(tr("Running engine..."));
    runTimer_.start();

    engine_->start(enginePath, engineArgs());
    if (!engine_->waitForStarted(5000)) {
        spinner_->stop();
        execBtn_->setEnabled(true);
        statusBar()->showMessage(tr("Unable to start engine process"));
        return;
    }
    engine_->write(dsl.toUtf8());
    engine_->closeWriteChannel();
}

void MainWindow::onEngineFinished(int, QProcess::ExitStatus) {
    spinner_->stop();
    execBtn_->setEnabled(true);

    QByteArray out = engine_->readAllStandardOutput();
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(out, &perr);

    // Warmup run: the cache was (re)built in the background; no results to show.
    if (warmupRunning_) {
        warmupRunning_ = false;
        bool rebuilt = engineErrBuf_.contains("[Cache] No cache index found")
                    || engineErrBuf_.contains("[Cache] Incremental update");
        if (rebuilt) qInfo() << "[Preprocess] inference cache built/updated";
        else        qInfo() << "[Preprocess] cache already up to date";
        refreshStats();
        refreshStatusBar();
        if (rebuilt) reloadCollectionPanel();   // cluster groups may have appeared
        return;
    }

    ++runs_;
    bool rebuilt = engineErrBuf_.contains("[Cache] No cache index found")
                || engineErrBuf_.contains("[Cache] Incremental update");
    if (!rebuilt) ++cacheHits_;
    qInfo() << (rebuilt ? "Engine finished (cache rebuilt)" : "Engine finished (cache hit)");

    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        statusBar()->showMessage(tr("Engine output is not valid JSON"));
        return;
    }
    if (rebuilt) reloadCollectionPanel();   // cluster groups may have appeared
    showResults(doc.object());
}

void MainWindow::onEngineStdErr() {
    QByteArray chunk = engine_->readAllStandardError();
    engineErrBuf_ += chunk;
    // Forward engine lines to the log panel (they include the [Cache]
    // preprocessing progress) so the user sees what is being processed.
    for (const QByteArray& line : chunk.split('\n')) {
        QString s = QString::fromUtf8(line).trimmed();
        if (!s.isEmpty()) qInfo().noquote() << s;
    }
}

void MainWindow::showResults(const QJsonObject& obj) {
    photoRoots_.clear();
    for (const QJsonValue& v : obj["photo_roots"].toArray()) {
        QJsonObject r = v.toObject();
        QString prefix = r["prefix"].toString();
        photoRoots_[prefix.isEmpty() ? QString() : prefix] = r["dir"].toString();
    }

    QString type = obj["type"].toString();
    if (type == "error") {
        statusBar()->showMessage(tr("Engine error: %1").arg(obj["message"].toString()));
        refreshStats();
        return;
    }
    if (type == "scalar") {
        statusBar()->showMessage(tr("Result (scalar): %1").arg(obj["value"].toDouble()));
        refreshStats();
        return;
    }

    pending_.clear();
    for (const QJsonValue& v : obj["results"].toArray()) {
        QJsonObject r = v.toObject();
        QString path = r["path"].toString();
        QString full;
        int slash = path.indexOf('/');
        if (slash > 0 && photoRoots_.contains(path.left(slash))) {
            full = photoRoots_[path.left(slash)] + "/" + path.mid(slash + 1);
        } else {
            QString dir = photoRoots_.value(QString());
            if (dir.isEmpty()) dir = obj["photo_dir"].toString();
            full = dir + "/" + path;
        }
        pending_.push_back({path, r["score"].toDouble(), full});
    }

    // descending (highest probability first)
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const ResultItem& a, const ResultItem& b) {
                         if (a.score != b.score) return a.score > b.score;
                         return a.path < b.path;
                     });

    // count low-score items to be filtered
    filteredLow_ = 0;
    for (const auto& it : pending_) {
        if (it.score < 0.05) ++filteredLow_;
    }

    thumbPos_ = 0;
    resultGrid_->clear();
    int matched = (int)pending_.size();
    int shown = matched - filteredLow_;
    statusBar()->showMessage(tr("%1 matches (showing %2, %3 filtered as low-score)")
                                 .arg(matched).arg(shown).arg(filteredLow_));
    qInfo() << QString("Search finished: %1 matches, %2 shown, %3 ms")
                   .arg(matched).arg(shown).arg(runTimer_.elapsed());
    refreshStats();
    thumbTimer_->start();
}

void MainWindow::onThumbnailTick() {
    int count = 0;
    while (thumbPos_ < pending_.size() && count < 8) {
        const ResultItem& it = pending_[thumbPos_];
        ++thumbPos_;
        // probability filter: hide low-score results (< 0.05)
        if (it.score < 0.05) continue;

        QPixmap pm;
        if (!it.fullPath.isEmpty() && QFile::exists(it.fullPath)) {
            pm.load(it.fullPath);
            if (!pm.isNull()) pm = pm.scaled(QSize(120, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        QListWidgetItem* item = new QListWidgetItem(resultGrid_);
        item->setSizeHint(QSize(160, 170));
        item->setData(Qt::UserRole, it.path);
        item->setData(Qt::UserRole + 1, it.fullPath);
        resultGrid_->addItem(item);
        resultGrid_->setItemWidget(item, new ResultItemWidget(pm, it.score, it.path, showScores_));
        ++count;
    }
    if (thumbPos_ >= pending_.size()) thumbTimer_->stop();
}

void MainWindow::onResultDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString rel = item->data(Qt::UserRole).toString();
    QString full = item->data(Qt::UserRole + 1).toString();
    ImageDetailsDialog dlg(rel, full, this);
    dlg.exec();
    if (dlg.tagsChanged()) {
        qInfo() << "Tags updated for: " << rel;
    }
}

void MainWindow::onTagFilterClicked() {
    TagFilterDialog dlg(tagFilters_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    tagFilters_ = dlg.filters();
    saveTagFilters();
    if (tagFilters_.isEmpty()) {
        statusBar()->showMessage(tr("Tag filter cleared — full library search."));
    } else {
        statusBar()->showMessage(tr("Tag filter active: %1").arg(tagFilterSummary()));
    }
}

void MainWindow::loadSavedTagFilters() {
    tagFilters_.clear();
    for (const QString& spec : SettingsManager::instance()->tagFilters()) {
        QString key = spec, vals;
        int eq = spec.indexOf('=');
        if (eq >= 0) {
            key = spec.left(eq);
            vals = spec.mid(eq + 1);
        }
        if (key.trimmed().isEmpty()) continue;
        QStringList values;
        if (!vals.isEmpty()) values = vals.split('|', Qt::KeepEmptyParts);
        tagFilters_.append({key, values});
    }
}

void MainWindow::saveTagFilters() {
    QStringList specs;
    for (const auto& f : tagFilters_) {
        specs << (f.first + "=" + f.second.join('|'));
    }
    SettingsManager::instance()->setTagFilters(specs);
}

QString MainWindow::tagFilterSummary() const {
    QStringList summary;
    for (const auto& f : tagFilters_) {
        summary << (f.first + "=" + (f.second.isEmpty() ? QString("*") : f.second.join('|')));
    }
    return summary.join(" ; ");
}

void MainWindow::removeImagesFromCache(const QStringList& relPaths) {
    if (relPaths.isEmpty()) return;
    QString cacheFile = SettingsManager::projectDir() + "/cache/"
                        + ModelManager::instance()->activeModel() + "/cache_index.json";
    QFile f(cacheFile);
    if (!f.open(QIODevice::ReadWrite)) return;
    QJsonParseError perr;
    QByteArray raw = f.readAll();
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        raw = raw.mid(3);
    }
    QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) { f.close(); return; }
    QJsonObject root = doc.object();
    QJsonObject entries = root["entries"].toObject();
    bool changed = false;
    for (const QString& rel : relPaths) {
        if (entries.contains(rel)) {
            entries.remove(rel);
            changed = true;
        }
    }
    if (changed) {
        root["entries"] = entries;
        f.resize(0);
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    f.close();
}

void MainWindow::onDeleteSelected() {
    QList<QListWidgetItem*> sel = resultGrid_->selectedItems();
    if (sel.isEmpty()) {
        statusBar()->showMessage(tr("No image selected."));
        return;
    }
    QStringList relPaths, fullPaths;
    for (QListWidgetItem* it : sel) {
        relPaths << it->data(Qt::UserRole).toString();
        fullPaths << it->data(Qt::UserRole + 1).toString();
    }
    auto r = QMessageBox::warning(
        this, tr("Delete Confirmation"),
        tr("确定要删除 %1 张图片吗？此操作将删除磁盘文件并清除缓存，不可撤销。")
            .arg(sel.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    // 1) delete files from disk
    for (const QString& full : fullPaths) {
        if (!full.isEmpty() && QFile::exists(full)) QFile::remove(full);
    }
    // 2) remove cache entries
    removeImagesFromCache(relPaths);
    // 3) refresh the grid
    for (QListWidgetItem* it : sel) {
        delete resultGrid_->takeItem(resultGrid_->row(it));
    }
    qInfo() << "Deleted " << sel.size() << " selected image(s).";
    refreshStats();
    statusBar()->showMessage(tr("Deleted %1 image(s).").arg(sel.size()));
}