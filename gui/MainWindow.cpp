#include "MainWindow.h"
#include "Logger.h"
#include "LanguageManager.h"
#include "ImageDetailsDialog.h"
#include "TagFilterDialog.h"
#include "managers/SettingsManager.h"
#include "managers/ModelManager.h"
#include "managers/ExtensionManager.h"
#include "managers/LibraryManager.h"
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
#include <QMessageBox>
#include <QFontDatabase>
#include <QAction>
#include <QEvent>
#include <QRegularExpression>

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
        tr("Models"), tr("Extensions"), tr("Logs")};
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
    resultGrid_ = new QListWidget(searchPage_);
    resultGrid_->setViewMode(QListView::IconMode);
    resultGrid_->setResizeMode(QListView::Adjust);
    resultGrid_->setSelectionMode(QAbstractItemView::ExtendedSelection);  // multi-select
    resultGrid_->setSpacing(10);
    resultGrid_->setIconSize(QSize(120, 120));
    resultGrid_->setGridSize(QSize(160, 170));
    resultGrid_->setWordWrap(false);
    resultGrid_->setMovement(QListView::Static);
    connect(resultGrid_, &QListWidget::itemDoubleClicked, this, &MainWindow::onResultDoubleClicked);
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

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);
    mainLay->addWidget(statsBar);
    mainLay->addWidget(stack_, 1);
    setCentralWidget(central);

    statusBar()->addWidget(new QLabel(" ", this), 1);
    statusBar()->showMessage(tr("Ready"));
    resize(1120, 720);

    connect(translateBtn_, &QPushButton::clicked, this, &MainWindow::onTranslateClicked);
    connect(execBtn_, &QPushButton::clicked, this, &MainWindow::onExecuteClicked);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &MainWindow::onTranslateClicked);
}

QLabel* MainWindow::createStatLabel(const QString& text) {
    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet(
        "background: #2d2d30; color: #d4d4d4; padding: 2px 10px; border-radius: 10px; border: 1px solid #3e3e42;");
    return lbl;
}

void MainWindow::applyTheme(bool dark) {
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
    QJsonArray classes = QJsonDocument::fromJson(f.readAll()).object()["classes"].toArray();
    QHash<QString, QStringList> byParent;
    for (const QJsonValue& v : classes) {
        QJsonObject c = v.toObject();
        QString parent = c["parent"].toString();
        if (parent.isEmpty()) parent = "root";
        byParent[parent].append(c["name"].toString());
    }
    QStringList lines;
    QStringList keys = byParent.keys();
    keys.sort();
    for (const QString& p : keys) {
        QStringList kids = byParent.value(p);
        kids.sort();
        lines << p + " -> [" + kids.join(", ") + "]";
    }
    return lines.join("\n");
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
    QString dsl = dslEdit_->toPlainText();
    if (dsl.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("DSL is empty - translate or type it first"));
        return;
    }
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
        statusBar()->showMessage(tr("Engine is already running"));
        return;
    }

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

    ++runs_;
    bool rebuilt = engineErrBuf_.contains("[Cache] Building cache");
    if (!rebuilt) ++cacheHits_;
    qInfo() << (rebuilt ? "Engine finished (cache rebuilt)" : "Engine finished (cache hit)");

    QByteArray out = engine_->readAllStandardOutput();
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        statusBar()->showMessage(tr("Engine output is not valid JSON"));
        return;
    }
    showResults(doc.object());
}

void MainWindow::onEngineStdErr() {
    engineErrBuf_ += engine_->readAllStandardError();
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