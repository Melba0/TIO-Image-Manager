#pragma once
#include <QWidget>
#include <functional>
class QListWidget;
class QStackedWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QFormLayout;
class QLineEdit;
class QDoubleSpinBox;

// Embedded settings page (no dialog): left vertical navigation + right panels
// for 通用 / API / 图库 / 模型 / 扩展包 / 日志.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void setCurrentTab(int index);
    int currentTab() const;
    void retranslateUi();

signals:
    void settingsChanged();   // anything changed; UI/status should refresh

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildGeneralPanel();
    QWidget* buildApiPanel();
    QWidget* buildLibraryPanel();
    QWidget* buildModelsPanel();
    QWidget* buildInferencePanel();
    QWidget* buildExtensionsPanel();

    // re-translatable widgets (text is re-applied in retranslateUi)
    QLabel* generalTitle_ = nullptr;
    QLabel* langLabel_ = nullptr;
    QCheckBox* autoSearch_ = nullptr;
    QPushButton* testBtn_ = nullptr;
    QFormLayout* apiForm_ = nullptr;
    QLineEdit* apiUrlEdit_ = nullptr;
    QLineEdit* apiKeyEdit_ = nullptr;
    QLineEdit* apiModelEdit_ = nullptr;
    QPushButton* addFolderBtn_ = nullptr;
    QPushButton* delFolderBtn_ = nullptr;
    QPushButton* reindexBtn_ = nullptr;
    QLabel* modelsHint_ = nullptr;
    QPushButton* addModelBtn_ = nullptr;
    QPushButton* delModelBtn_ = nullptr;
    QLabel* inferenceTitle_ = nullptr;
    QFormLayout* inferenceForm_ = nullptr;
    QDoubleSpinBox* baseConfSpin_ = nullptr;
    QDoubleSpinBox* iouSpin_ = nullptr;
    QDoubleSpinBox* fallbackSpin_ = nullptr;
    QLabel* inferenceHint_ = nullptr;
    QLabel* extHint_ = nullptr;
    QPushButton* addExtBtn_ = nullptr;
    QPushButton* delExtBtn_ = nullptr;

    QListWidget* nav_;
    QStackedWidget* stack_;

    std::function<void()> modelRefresh_;
    std::function<void()> extRefresh_;
    bool extRefreshing_ = false;   // guard against setActive -> scan -> refresh -> itemChanged re-entry
    int lastNavRow_ = 0;
};
