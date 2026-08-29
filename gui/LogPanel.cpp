#include "LogPanel.h"
#include "Logger.h"
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* lay = new QVBoxLayout(this);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel("级别:", this));
    levelFilter_ = new QComboBox(this);
    levelFilter_->addItems({"全部", "INFO", "WARNING", "ERROR"});
    toolbar->addWidget(levelFilter_);
    toolbar->addWidget(new QLabel("关键词:", this));
    keyword_ = new QLineEdit(this);
    keyword_->setPlaceholderText("过滤关键词...");
    toolbar->addWidget(keyword_, 1);
    QPushButton* clearBtn = new QPushButton("清空", this);
    QPushButton* exportBtn = new QPushButton("导出", this);
    toolbar->addWidget(clearBtn);
    toolbar->addWidget(exportBtn);
    lay->addLayout(toolbar);

    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    QFont mono = view_->font();
    mono.setFamily("Consolas");
    mono.setStyleHint(QFont::Monospace);
    view_->setFont(mono);
    lay->addWidget(view_, 1);

    connect(levelFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LogPanel::applyFilter);
    connect(keyword_, &QLineEdit::textChanged, this, &LogPanel::applyFilter);
    connect(clearBtn, &QPushButton::clicked, this, &LogPanel::onClear);
    connect(exportBtn, &QPushButton::clicked, this, &LogPanel::onExport);

    // load existing entries
    all_ = Logger::instance()->entries();
    view_->clear();
    for (const auto& e : all_) {
        if (passesFilter(e.first, e.second)) appendLine(e.first, e.second, false);
    }
    connect(Logger::instance(), &Logger::entryAdded, this, &LogPanel::onEntryAdded);
}

bool LogPanel::passesFilter(int level, const QString& text) const {
    int idx = levelFilter_->currentIndex();  // 0=all, 1=INFO, 2=WARNING, 3=ERROR
    if (idx >= 1 && level != idx - 1) return false;
    QString kw = keyword_->text().trimmed();
    if (!kw.isEmpty() && !text.contains(kw, Qt::CaseInsensitive)) return false;
    return true;
}

void LogPanel::appendLine(int level, const QString& text, bool scroll) {
    QString color = "#4fc1ff";   // INFO blue
    if (level == Logger::Warning) color = "#e5c07b";   // yellow
    else if (level == Logger::Error) color = "#e06c75"; // red
    view_->appendHtml(QString("<span style='color:%1;'>%2</span>").arg(color, text.toHtmlEscaped()));
    if (scroll) {
        QScrollBar* sb = view_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void LogPanel::onEntryAdded(int level, const QString& text) {
    all_.push_back({level, text});
    if ((int)all_.size() > 2000) all_.removeFirst();
    if (passesFilter(level, text)) appendLine(level, text, true);
}

void LogPanel::applyFilter() {
    view_->clear();
    for (const auto& e : all_) {
        if (passesFilter(e.first, e.second)) appendLine(e.first, e.second, false);
    }
    QScrollBar* sb = view_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void LogPanel::onClear() {
    view_->clear();   // display only; ring/file untouched
}

void LogPanel::onExport() {
    QString path = QFileDialog::getSaveFileName(this, "导出日志", "dsl_log.txt", "文本文件 (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (const auto& e : all_) ts << e.second << "\n";
    f.close();
}