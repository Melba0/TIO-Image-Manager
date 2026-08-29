#pragma once
#include <QDialog>
#include <QPair>
#include <QStringList>
#include <QVector>
class QLineEdit;
class QVBoxLayout;
class QWidget;

// "Filter Options" window for the tag pre-filter pipeline.  The user builds a
// list of (key, values) conditions; images whose user_tags match ALL conditions
// (values OR-ed) become the pre-filtered set that `$` iterates in the DSL.
class TagFilterDialog : public QDialog {
    Q_OBJECT
public:
    // `initial` pre-fills the condition rows with previously saved filters
    // (persisted across dialog reopenings / app restarts).
    explicit TagFilterDialog(const QVector<QPair<QString, QStringList>>& initial,
                             QWidget* parent = nullptr);

    // Active filters after the user clicked Apply (key, OR-ed values).
    QVector<QPair<QString, QStringList>> filters() const;

private:
    void addRow(const QString& key = QString(), const QStringList& values = {});
    void loadExistingKeys();
    void applyFilters();

    // A single condition row.
    struct Row {
        QWidget* widget = nullptr;
        QLineEdit* keyEdit = nullptr;
        QWidget* valuesWrap = nullptr;
        QVBoxLayout* valuesLayout = nullptr;
        QStringList values;
    };

    QVector<Row> rows_;
    QVBoxLayout* rowsLayout_ = nullptr;
    QStringList existingKeys_;
};
