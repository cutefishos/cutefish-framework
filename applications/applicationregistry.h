#pragma once

#include "desktopentry.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

class DesktopEntryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        GenericNameRole,
        CommentRole,
        IconRole,
        PathRole,
        ExecRole,
        CommandRole,
        StartupWMClassRole,
        CategoriesRole,
        KeywordsRole,
        MimeTypesRole,
        TerminalRole,
        EntryRole
    };
    Q_ENUM(Roles)

    explicit DesktopEntryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QList<DesktopEntry *> &entries);
    QList<DesktopEntry *> entries() const;

private:
    QList<DesktopEntry *> m_entries;
};

class ApplicationRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *applications READ applications CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    static ApplicationRegistry *instance();

    explicit ApplicationRegistry(QObject *parent = nullptr);

    QAbstractItemModel *applications() const;
    bool loading() const;

    Q_INVOKABLE DesktopEntry *byId(const QString &id) const;
    Q_INVOKABLE DesktopEntry *byPath(const QString &path) const;

    DesktopEntry *resolveWindow(const QString &appId,
                                const QString &windowClass,
                                const QString &executablePath,
                                const QString &executableName) const;
    QList<DesktopEntry *> applicationEntries() const;
    QList<DesktopEntry *> entries() const;
    static QStringList desktopPaths();

signals:
    void applicationsChanged();
    void loadingChanged();

public slots:
    void scheduleScan();
    void startScan();
    void applyScan(const QList<DesktopEntryData> &results);

private:
    void monitorPaths();

    DesktopEntryModel *m_model;
    QFileSystemWatcher *m_watcher;
    QTimer m_debounceTimer;
    QHash<QString, DesktopEntry *> m_entriesById;
    QHash<QString, DesktopEntry *> m_entriesByPath;
    QList<DesktopEntry *> m_entries;
    bool m_loading = false;
    bool m_scanInProgress = false;
    bool m_scanQueued = false;
};

void registerApplicationsQmlTypes();
