#pragma once
#include <QObject>
#include <QHash>
#include <QString>

// Persists user-facing display names for cluster ids in
// config/cluster_name_mappings.json (e.g.
// { "face_cluster_person_001": "张三" }).  The GUI shows the mapped name when
// available and the raw cluster id otherwise.
class ClusterNameMapping : public QObject {
    Q_OBJECT
public:
    static ClusterNameMapping* instance();
    static QString filePath();

    // Display name: the mapped name, or the raw cluster id with the
    // "<cluster_name>_" prefix stripped (e.g. "face_cluster_person_001" ->
    // "person_001").  When `clusterName` is omitted the id is shown as-is after
    // dropping only its first underscore segment.
    QString displayName(const QString& clusterId,
                        const QString& clusterName = QString()) const;
    bool hasMapping(const QString& clusterId) const;
    void setMapping(const QString& clusterId, const QString& displayName);
    void removeMapping(const QString& clusterId);

signals:
    void mappingsChanged();

private:
    explicit ClusterNameMapping(QObject* parent = nullptr);
    void reload();
    bool persist() const;
    QHash<QString, QString> map_;
};
