#pragma once

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <functional>

class QCheckBox;
class QDialog;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

// The owning window grants access only while all local save writers are idle.
class CloudSaves final : public QObject {
    Q_OBJECT
public:
    CloudSaves(QWidget *parent, std::function<QString()> root, std::function<bool()> idle);
    bool busy() const;
    void request();
    void show_settings();
    void tick();

signals:
    void busy_changed(bool busy);

private:
    bool safe() const;
    void refresh();
    void resolve(const QString &key, const QString &revision);
    std::function<QString()> m_root;
    std::function<bool()> m_idle;
    QPointer<QDialog> m_dialog;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_last_check = nullptr;
    QPushButton *m_sync = nullptr;
    QWidget *m_conflicts = nullptr;
    QVBoxLayout *m_conflict_layout = nullptr;
    QJsonArray m_versions;
    bool m_busy = false;
};
