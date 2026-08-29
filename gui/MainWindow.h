#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QElapsedTimer>
#include <vector>
#include "LlmClient.h"
#include "Spinner.h"
#include "SettingsPage.h"
#include "ResultItemWidget.h"

class QPlainTextEdit;
class QListWidget;
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
    void removeImagesFromCache(const QStringList& relPaths);
    void loadSavedTagFilters();
    void saveTagFilters();
    QString tagFilterSummary() const;

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
    QListWidget* resultGrid_;
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
    QVector<QPair<QString, QStringList>> tagFilters_;   // active tag pre-filter
};
