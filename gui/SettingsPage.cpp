#include "SettingsPage.h"
#include "LogPanel.h"
#include "Logger.h"
#include "LanguageManager.h"
#include "managers/SettingsManager.h"
#include "managers/ModelManager.h"
#include "managers/ExtensionManager.h"
#include "managers/LibraryManager.h"
#include "LlmClient.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidgetItem>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QShowEvent>

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    nav_ = new QListWidget(this);
    nav_->setObjectName("navList");
    nav_->setFixedWidth(160);
    for (int i = 0; i < 6; ++i) nav_->addItem(QString::number(i));
    nav_->setCurrentRow(0);
    lastNavRow_ = 0;
    root->addWidget(nav_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildGeneralPanel());     // 0
    stack_->addWidget(buildApiPanel());         // 1
    stack_->addWidget(buildLibraryPanel());     // 2
    stack_->addWidget(buildModelsPanel());      // 3
    stack_->addWidget(buildExtensionsPanel());  // 4
    stack_->addWidget(new LogPanel(this));      // 5
    root->addWidget(stack_, 1);

    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    // Keep the current-page highlight when the user clicks empty space below the items.
    connect(nav_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur, QListWidgetItem*) {
        if (cur) {
            lastNavRow_ = nav_->row(cur);
        } else {
            nav_->setCurrentRow(lastNavRow_);
        }
    });

    // Re-scan whenever the managers change (e.g. from the main window) and
    // refresh the two lists automatically.
    connect(ModelManager::instance(), &ModelManager::modelsChanged, this, [this]() {
        if (modelRefresh_) modelRefresh_();
    });
    connect(ExtensionManager::instance(), &ExtensionManager::packsChanged, this, [this]() {
        if (extRefresh_) extRefresh_();
    });

    connect(LanguageManager::instance(), &LanguageManager::languageChanged,
            this, &SettingsPage::retranslateUi);
    retranslateUi();
}

void SettingsPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Ensure model / extension lists are populated every time the page is shown.
    ModelManager::instance()->scan();
    ExtensionManager::instance()->scan();
}

void SettingsPage::retranslateUi() {
    const QStringList items = {tr("General"), tr("API Config"), tr("Library"),
                               tr("Models"), tr("Extensions"), tr("Logs")};
    for (int i = 0; i < items.size(); ++i) {
        QListWidgetItem* it = nav_->item(i);
        if (it) it->setText(items[i]);
    }
    if (generalTitle_) generalTitle_->setText(tr("General Settings"));
    if (langLabel_) langLabel_->setText(tr("Language:"));
    if (autoSearch_) autoSearch_->setText(tr("Auto-load last search on startup"));
    if (testBtn_) testBtn_->setText(tr("Test Connection"));
    if (apiForm_) {
        if (apiUrlEdit_) {
            if (auto* l = qobject_cast<QLabel*>(apiForm_->labelForField(apiUrlEdit_))) l->setText(tr("Base URL:"));
        }
        if (apiKeyEdit_) {
            if (auto* l = qobject_cast<QLabel*>(apiForm_->labelForField(apiKeyEdit_))) l->setText(tr("API Key:"));
        }
        if (apiModelEdit_) {
            if (auto* l = qobject_cast<QLabel*>(apiForm_->labelForField(apiModelEdit_))) l->setText(tr("Model Name:"));
        }
    }
    if (addFolderBtn_) addFolderBtn_->setText(tr("Add Folder"));
    if (delFolderBtn_) delFolderBtn_->setText(tr("Remove Selected"));
    if (reindexBtn_) reindexBtn_->setText(tr("Reindex"));
    if (modelsHint_) modelsHint_->setText(tr("Double-click an item to switch the active model (writes registry.json):"));
    if (addModelBtn_) addModelBtn_->setText(tr("Add Model"));
    if (delModelBtn_) delModelBtn_->setText(tr("Remove Model"));
    if (extHint_) extHint_->setText(tr("Check to enable/disable an extension pack (writes registry.json):"));
    if (addExtBtn_) addExtBtn_->setText(tr("Add Extension"));
    if (delExtBtn_) delExtBtn_->setText(tr("Remove Extension"));
}

void SettingsPage::setCurrentTab(int index) {
    if (index >= 0 && index < nav_->count()) {
        nav_->setCurrentRow(index);
    }
}

int SettingsPage::currentTab() const {
    return nav_->currentRow();
}

// ---------------- 通用 ----------------
QWidget* SettingsPage::buildGeneralPanel() {
    QWidget* w = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(w);
    QLabel* title = new QLabel(w);
    title->setText(tr("General Settings"));
    generalTitle_ = title;
    QFont f = title->font();
    f.setBold(true);
    f.setPointSize(13);
    title->setFont(f);
    lay->addWidget(title);

    QFormLayout* form = new QFormLayout();
    QComboBox* lang = new QComboBox(w);
    lang->addItems({"English", "简体中文"});
    lang->setCurrentIndex(LanguageManager::instance()->language() == "zh" ? 1 : 0);
    connect(lang, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [](int idx) { LanguageManager::instance()->setLanguage(idx == 1 ? "zh" : "en"); });
    langLabel_ = new QLabel(w);
    form->addRow(langLabel_, lang);
    lay->addLayout(form);

    QCheckBox* autoSearch = new QCheckBox(tr("Auto-load last search on startup"), w);
    autoSearch_ = autoSearch;
    autoSearch->setChecked(SettingsManager::instance()->value("general/auto_load_last_search").toBool());
    connect(autoSearch, &QCheckBox::toggled, this, [](bool b) {
        SettingsManager::instance()->setValue("general/auto_load_last_search", b);
    });
    lay->addWidget(autoSearch);
    lay->addStretch();
    return w;
}

// ---------------- API ----------------
QWidget* SettingsPage::buildApiPanel() {
    QWidget* w = new QWidget(this);
    QFormLayout* form = new QFormLayout(w);
    apiForm_ = form;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QLineEdit* urlEdit = new QLineEdit(w);
    urlEdit->setPlaceholderText("https://api.openai.com/v1/chat/completions");
    apiUrlEdit_ = urlEdit;
    QLineEdit* keyEdit = new QLineEdit(w);
    keyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit_ = keyEdit;
    QLineEdit* modelEdit = new QLineEdit(w);
    apiModelEdit_ = modelEdit;
    QPushButton* testBtn = new QPushButton(tr("Test Connection"), w);
    testBtn_ = testBtn;

    SettingsManager* s = SettingsManager::instance();
    urlEdit->setText(s->baseUrl());
    keyEdit->setText(s->apiKey());
    keyEdit->setPlaceholderText(SettingsManager::maskKey(s->apiKey()));
    modelEdit->setText(s->modelName());

    connect(urlEdit, &QLineEdit::textChanged, s, &SettingsManager::setBaseUrl);
    connect(keyEdit, &QLineEdit::textChanged, s, &SettingsManager::setApiKey);
    connect(modelEdit, &QLineEdit::textChanged, s, &SettingsManager::setModelName);

    form->addRow(tr("Base URL:"), urlEdit);
    form->addRow(tr("API Key:"), keyEdit);
    form->addRow(tr("Model Name:"), modelEdit);
    form->addRow("", testBtn);

    connect(testBtn, &QPushButton::clicked, this, [w, testBtn, s]() {
        testBtn->setEnabled(false);
        testBtn->setText("连接中...");
        static LlmClient* client = new LlmClient(w);
        auto done = [testBtn]() {
            testBtn->setEnabled(true);
            testBtn->setText("测试连接");
        };
        connect(client, &LlmClient::connectionOk, w, [done]() {
            done();
            QMessageBox::information(nullptr, tr("Connection Test"), tr("Connection OK"));
            qInfo() << "API 连接测试成功";
        });
        connect(client, &LlmClient::requestFailed, w, [done](const QString& msg) {
            done();
            QMessageBox::warning(nullptr, tr("Connection Failed"), msg);
            qWarning() << "API 连接测试失败: " << msg;
        });
        client->testConnection(s->baseUrl(), s->apiKey(), s->modelName());
    });
    return w;
}

// ---------------- 图库 ----------------
QWidget* SettingsPage::buildLibraryPanel() {
    QWidget* w = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(w);

    QListWidget* list = new QListWidget(w);
    lay->addWidget(list, 1);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* add = new QPushButton(tr("Add Folder"), w);
    addFolderBtn_ = add;
    QPushButton* del = new QPushButton(tr("Remove Selected"), w);
    delFolderBtn_ = del;
    QPushButton* reindex = new QPushButton(tr("Reindex"), w);
    reindexBtn_ = reindex;
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addWidget(reindex);
    btns->addStretch();
    lay->addLayout(btns);

    auto refresh = [list]() {
        list->clear();
        for (const QString& p : LibraryManager::instance()->paths()) {
            QListWidgetItem* it = new QListWidgetItem(p, list);
            it->setData(Qt::UserRole, p);
        }
    };
    refresh();

    connect(add, &QPushButton::clicked, this, [this, list, refresh]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择图库文件夹");
        if (dir.isEmpty()) return;
        LibraryManager::instance()->addPath(dir);
        refresh();
        qInfo() << "添加图库路径: " << dir;
        emit settingsChanged();
    });
    connect(del, &QPushButton::clicked, this, [this, list, refresh]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        QString p = it->data(Qt::UserRole).toString();
        LibraryManager::instance()->removePath(p);
        refresh();
        qInfo() << "移除图库路径: " << p;
        emit settingsChanged();
    });
    connect(reindex, &QPushButton::clicked, this, [this]() {
        QString active = ModelManager::instance()->activeModel();
        QString cacheDir = SettingsManager::projectDir() + "/cache/" + active;
        QDir d(cacheDir);
        if (d.exists() && d.removeRecursively()) {
            QMessageBox::information(this, "重新索引",
                                     QString("已清空模型 %1 的缓存，下次执行检索时自动重新索引。").arg(active));
            qInfo() << "重新索引: 已清空缓存 " << cacheDir;
        }
        emit settingsChanged();
    });
    return w;
}

// ---------------- 模型 ----------------
QWidget* SettingsPage::buildModelsPanel() {
    QWidget* w = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(w);
    QLabel* hint = new QLabel(w);
    hint->setText(tr("Double-click an item to switch the active model (writes registry.json):"));
    modelsHint_ = hint;
    lay->addWidget(hint);

    QListWidget* list = new QListWidget(w);
    lay->addWidget(list, 1);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* add = new QPushButton(tr("Add Model"), w);
    addModelBtn_ = add;
    QPushButton* del = new QPushButton(tr("Remove Model"), w);
    delModelBtn_ = del;
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    lay->addLayout(btns);

    ModelManager* mgr = ModelManager::instance();
    auto refresh = [list]() {
        list->clear();
        ModelManager* m = ModelManager::instance();
        QString active = m->activeModel();
        for (const ModelInfo& mi : m->models()) {
            QString text = mi.name + QString("   (input=%1, classes=%2)").arg(mi.inputSize).arg(mi.classes);
            bool act = (mi.name == active);
            text += act ? "   [ACTIVE]" : "   [INACTIVE]";
            QListWidgetItem* it = new QListWidgetItem(text, list);
            it->setData(Qt::UserRole, mi.name);
            if (act) {
                it->setForeground(QColor(0x4f, 0xc1, 0x4f));   // green highlight
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
        }
    };
    modelRefresh_ = refresh;
    mgr->scan();   // make sure models_ is populated before the first refresh
    refresh();

    connect(list, &QListWidget::itemDoubleClicked, this, [this, list, refresh](QListWidgetItem* it) {
        QString name = it->data(Qt::UserRole).toString();
        if (ModelManager::instance()->setActiveModel(name)) {
            QMessageBox::information(this, "切换模型",
                                     QString("已切换至 %1，缓存将自动重建（首次检索会稍慢）。").arg(name));
            qInfo() << "切换激活模型: " << name;
            refresh();
            emit settingsChanged();
        }
    });
    connect(add, &QPushButton::clicked, this, [this, refresh]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择模型包文件夹（需包含 model.onnx / meta.json / classes.json）");
        if (dir.isEmpty()) return;
        if (!QFile::exists(dir + "/classes.json")) {
            QMessageBox::warning(this, "添加模型", "所选文件夹缺少 classes.json");
            return;
        }
        if (ModelManager::instance()->addModel(dir)) {
            refresh();
            qInfo() << "添加模型包: " << dir;
            emit settingsChanged();
        } else {
            QMessageBox::warning(this, "添加模型", "添加失败（文件夹可能已存在或缺少 model.onnx）");
        }
    });
    connect(del, &QPushButton::clicked, this, [this, list, refresh]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        QString name = it->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, "删除模型", QString("确定删除模型包 %1？（将删除文件夹）").arg(name))
            != QMessageBox::Yes) return;
        ModelManager::instance()->removeModel(name);
        refresh();
        qInfo() << "删除模型包: " << name;
        emit settingsChanged();
    });
    return w;
}

// ---------------- 扩展包 ----------------
QWidget* SettingsPage::buildExtensionsPanel() {
    QWidget* w = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(w);
    QLabel* hint = new QLabel(w);
    hint->setText(tr("Check to enable/disable an extension pack (writes registry.json):"));
    extHint_ = hint;
    lay->addWidget(hint);

    QListWidget* list = new QListWidget(w);
    lay->addWidget(list, 1);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* add = new QPushButton(tr("Add Extension"), w);
    addExtBtn_ = add;
    QPushButton* del = new QPushButton(tr("Remove Extension"), w);
    delExtBtn_ = del;
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    lay->addLayout(btns);

    ExtensionManager* mgr = ExtensionManager::instance();
    auto refresh = [this, list]() {
        extRefreshing_ = true;   // block itemChanged -> setActive re-entry while rebuilding
        list->clear();
        ExtensionManager* e = ExtensionManager::instance();
        for (const ExtPack& p : e->packs()) {
            QString text = QString("%1  (父类: %2, 子类: %3)").arg(p.name, p.parentClass, p.children.join(", "));
            QListWidgetItem* it = new QListWidgetItem(text, list);
            it->setData(Qt::UserRole, p.name);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(p.active ? Qt::Checked : Qt::Unchecked);
            // dependency warning: parent class not present in the active base model
            bool parentKnown = false;
            QFile reg(SettingsManager::projectDir() + "/models/registry.json");
            if (reg.open(QIODevice::ReadOnly)) {
                QString active = QJsonDocument::fromJson(reg.readAll()).object()["active_base"].toString();
                QFile cls(SettingsManager::projectDir() + "/models/base/" + active + "/classes.json");
                if (cls.open(QIODevice::ReadOnly)) {
                    for (const QJsonValue& v : QJsonDocument::fromJson(cls.readAll()).object()["classes"].toArray()) {
                        QJsonObject c = v.toObject();
                        if (c["name"].toString() == p.parentClass || c["parent"].toString() == p.parentClass) {
                            parentKnown = true;
                            break;
                        }
                    }
                }
            }
            if (!parentKnown && !p.parentClass.isEmpty()) {
                it->setText(text + QString("   ⚠ 父类 \"%1\" 不在当前基座模型中").arg(p.parentClass));
                it->setForeground(QColor(0xe0, 0x6c, 0x75));   // red warning
            }
        }
        extRefreshing_ = false;
    };
    extRefresh_ = refresh;
    mgr->scan();   // make sure packs_ is populated before the first refresh
    refresh();

    connect(list, &QListWidget::itemChanged, this, [this, list](QListWidgetItem* it) {
        if (extRefreshing_) return;   // programmatic change from refresh(), not a user toggle
        QString name = it->data(Qt::UserRole).toString();
        bool on = (it->checkState() == Qt::Checked);
        if (ExtensionManager::instance()->setActive(name, on)) {
            qInfo() << (on ? "启用" : "禁用") << "扩展包: " << name;
            emit settingsChanged();
        }
    });
    connect(add, &QPushButton::clicked, this, [this, refresh]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择扩展包文件夹（需包含 config.json）");
        if (dir.isEmpty()) return;
        if (ExtensionManager::instance()->addPack(dir)) {
            refresh();
            qInfo() << "添加扩展包: " << dir;
            emit settingsChanged();
        } else {
            QMessageBox::warning(this, "添加扩展包", "添加失败（文件夹可能已存在或缺少 config.json）");
        }
    });
    connect(del, &QPushButton::clicked, this, [this, list, refresh]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        QString name = it->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, "删除扩展包", QString("确定删除扩展包 %1？").arg(name))
            != QMessageBox::Yes) return;
        ExtensionManager::instance()->removePack(name);
        refresh();
        qInfo() << "删除扩展包: " << name;
        emit settingsChanged();
    });
    return w;
}
