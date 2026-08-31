#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QElapsedTimer>
#include <QListWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <vector>
#include "LlmClient.h"
#include "Spinner.h"
#include "SettingsPage.h"
#include "ResultItemWidget.h"

class QPlainTextEdit;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QGroupBox;
class QLabel;
class QStackedWidget;
class QComboBox;
class QAction;
class QTimer;

struct ResultItem {
    QString path;
    double score;
    QString fullPath;
};

// Result grid that advertises the selected images' relative paths when dragged
// (custom MIME type "application/x-tio-images", newline separated) so the user
// can drag images from the grid onto a collection in the left panel.
class ImageGrid : public QListWidget {
    Q_OBJECT
public:
    explicit ImageGrid(QWidget* parent = nullptr) : QListWidget(parent) {}
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// Left-side collection tree that accepts image drags from the grid and emits
// imagesDropped(name, paths) when they land on a normal collection item.
class CollectionTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit CollectionTree(QWidget* parent = nullptr) : QTreeWidget(parent) {}
signals:
    void imagesDropped(const QString& collection, const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onTranslateClicked();
    void onExecuteClicked();
    void onLlmResult(const QString& dsl);
    void onLlmError(const QString& msg);
    void onEngineFinished(int exitCode, QProcess::ExitStatus status);
    void onEngineStdErr();
    void onThumbnailTick();
    void onLanguageChanged(int index);
    void gotoSettings(int tab);
    void refreshView();
    void reindexCache();
    void toggleDarkMode();
    void showAbout();
    void onResultDoubleClicked(QListWidgetItem* item);
    void onDeleteSelected();
    void onTagFilterClicked();

    // ---- collections / smart collections ----
    void reloadCollectionPanel();
    void openCollection(const QString& name);
    void onCollectionItemClicked(QTreeWidgetItem* item, int column);
    void onCollectionItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCollectionContextMenu(const QPoint& pos);
    void onNewCollection();
    void onRenameCollection(QTreeWidgetItem* item);
    void onDeleteCollection(QTreeWidgetItem* item);
    void onRenameCluster(QTreeWidgetItem* item);
    void onAddToCollection(const QString& name);
    void onRemoveFromCollection();
    void onImagesDropped(const QString& collection, const QStringList& paths);
    void onSaveSmartCollection();
    void showClusterImages(const QString& clusterName, const QString& clusterId);

    // ---- export / batch edit ----
    void onExportReport();
    void onBatchEdit();
    void onGridContextMenu(const QPoint& pos);

private:
    void buildUi();
    void retranslateUi();
    QLabel* createStatLabel(const QString& text);
    void refreshStats();
    void applyTheme(bool dark);
    QString buildClassesSummary() const;
    QString buildExtensionsSummary() const;
    void showResults(const QJsonObject& obj);
    void refreshStatusBar();
    QStringList engineArgs() const;
    void executeDsl(const QString& dsl);
    void removeImagesFromCache(const QStringList& relPaths);
    void loadSavedTagFilters();
    void saveTagFilters();
    QString tagFilterSummary() const;
    void startWarmup();
    QWidget* buildCollectionPanel();
    QStringList selectedGridPaths() const;

    // search UI
    QWidget* searchPage_;
    QLineEdit* searchEdit_;
    QPushButton* translateBtn_;
    QPushButton* execBtn_;
    QComboBox* langBox_;
    QPushButton* deleteBtn_;
    QPushButton* tagFilterBtn_;
    Spinner* spinner_;
    QGroupBox* dslBox_;
    QPlainTextEdit* dslEdit_;
    ImageGrid* resultGrid_;
    // stats
    QLabel* statLibrary_;
    QLabel* statModel_;
    QLabel* statExt_;
    QLabel* statCache_;
    // shell
    QStackedWidget* stack_;
    SettingsPage* settingsPage_;
    QAction* showDslAct_;
    QAction* showScoresAct_;

    // ---- collections panel + toolbar ----
    QSplitter* mainSplitter_;
    QWidget* leftPanel_;
    CollectionTree* collectionTree_;
    QLabel* collectionTitleLabel_;
    QAction* saveSmartAct_;
    QAction* exportAct_;
    QAction* batchAct_;
    QString currentCollectionName_;   // name of the collection being browsed ("" = none)

    LlmClient* llm_;
    QProcess* engine_;
    QTimer* thumbTimer_;
    std::vector<ResultItem> pending_;
    size_t thumbPos_ = 0;
    int filteredLow_ = 0;
    QElapsedTimer runTimer_;
    QHash<QString, QString> photoRoots_;
    QByteArray engineErrBuf_;
    int runs_ = 0, cacheHits_ = 0;
    bool showScores_ = true;
    bool warmupRunning_ = false;   // engine launched with --warmup at startup
    QVector<QPair<QString, QStringList>> tagFilters_;   // active tag pre-filter
};
