#include "applicationregistry.h"
#include "applicationruntime.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QThreadPool>

#include <algorithm>
#include <utility>

namespace {

class DesktopEntryScanner : public QRunnable
{
public:
    explicit DesktopEntryScanner(ApplicationRegistry *registry)
        : m_registry(registry)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QList<DesktopEntryData> results;
        QSet<QString> selectedIds;

        for (const QString &rootPath : ApplicationRegistry::desktopPaths()) {
            QDir root(rootPath);
            if (!root.exists())
                continue;

            QStringList files;
            QDirIterator iterator(rootPath, {QStringLiteral("*.desktop")},
                                  QDir::Files | QDir::Readable,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext())
                files.append(iterator.next());
            std::sort(files.begin(), files.end());

            for (const QString &path : files) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    continue;

                QString id = root.relativeFilePath(path);
                id.replace(QLatin1Char('/'), QLatin1Char('-'));
                if (id.endsWith(QStringLiteral(".desktop")))
                    id.chop(QStringLiteral(".desktop").size());

                if (selectedIds.contains(id))
                    continue;

                DesktopEntryData data;
                if (DesktopEntry::parse(id, path, file.readAll(), &data)) {
                    selectedIds.insert(id);
                    results.append(std::move(data));
                }
            }
        }

        QPointer<ApplicationRegistry> registry = m_registry;
        QMetaObject::invokeMethod(m_registry, [registry, results]() {
            if (registry)
                registry->applyScan(results);
        }, Qt::QueuedConnection);
    }

private:
    ApplicationRegistry *m_registry;
};

QString normalized(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

DesktopEntryModel::DesktopEntryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DesktopEntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DesktopEntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    DesktopEntry *entry = m_entries.at(index.row());
    switch (role) {
    case IdRole: return entry->id();
    case NameRole: return entry->name();
    case GenericNameRole: return entry->genericName();
    case CommentRole: return entry->comment();
    case IconRole: return entry->icon();
    case PathRole: return entry->path();
    case ExecRole: return entry->exec();
    case CommandRole: return entry->command();
    case StartupWMClassRole: return entry->startupWMClass();
    case CategoriesRole: return entry->categories();
    case KeywordsRole: return entry->keywords();
    case MimeTypesRole: return entry->mimeTypes();
    case TerminalRole: return entry->terminal();
    case EntryRole: return QVariant::fromValue(entry);
    default: return {};
    }
}

QHash<int, QByteArray> DesktopEntryModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {GenericNameRole, "genericName"},
        {CommentRole, "comment"},
        {IconRole, "icon"},
        {PathRole, "path"},
        {ExecRole, "exec"},
        {CommandRole, "command"},
        {StartupWMClassRole, "startupWMClass"},
        {CategoriesRole, "categories"},
        {KeywordsRole, "keywords"},
        {MimeTypesRole, "mimeTypes"},
        {TerminalRole, "terminal"},
        {EntryRole, "entry"}
    };
}

void DesktopEntryModel::setEntries(const QList<DesktopEntry *> &entries)
{
    for (DesktopEntry *entry : std::as_const(m_entries))
        disconnect(entry, nullptr, this, nullptr);

    beginResetModel();
    m_entries = entries;
    endResetModel();

    for (DesktopEntry *entry : std::as_const(m_entries)) {
        connect(entry, &DesktopEntry::changed, this, [this, entry]() {
            const int row = m_entries.indexOf(entry);
            if (row >= 0)
                emit dataChanged(index(row), index(row));
        });
    }
}

QList<DesktopEntry *> DesktopEntryModel::entries() const
{
    return m_entries;
}

ApplicationRegistry *ApplicationRegistry::instance()
{
    static ApplicationRegistry *registry = new ApplicationRegistry;
    return registry;
}

ApplicationRegistry::ApplicationRegistry(QObject *parent)
    : QObject(parent)
    , m_model(new DesktopEntryModel(this))
    , m_watcher(new QFileSystemWatcher(this))
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(100);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ApplicationRegistry::scheduleScan);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ApplicationRegistry::scheduleScan);
    connect(&m_debounceTimer, &QTimer::timeout,
            this, &ApplicationRegistry::startScan);

    monitorPaths();
    startScan();
}

QAbstractItemModel *ApplicationRegistry::applications() const
{
    return m_model;
}

bool ApplicationRegistry::loading() const
{
    return m_loading;
}

DesktopEntry *ApplicationRegistry::byId(const QString &id) const
{
    if (DesktopEntry *entry = m_entriesById.value(id))
        return entry;

    const QString lower = normalized(id);
    for (auto it = m_entriesById.cbegin(); it != m_entriesById.cend(); ++it) {
        if (normalized(it.key()) == lower)
            return it.value();
    }
    return nullptr;
}

DesktopEntry *ApplicationRegistry::byPath(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    return m_entriesByPath.value(QFileInfo(path).absoluteFilePath());
}

DesktopEntry *ApplicationRegistry::resolveWindow(const QString &appId,
                                                 const QString &windowClass,
                                                 const QString &executablePath,
                                                 const QString &executableName) const
{
    const QString wantedAppId = normalized(appId);
    const QString wantedClass = normalized(windowClass);
    const QString wantedExecutable = normalized(executablePath);
    const QString wantedName = normalized(executableName);
    DesktopEntry *best = nullptr;
    int bestScore = 0;

    for (DesktopEntry *entry : applicationEntries()) {
        const QString id = normalized(entry->id());
        const QString startupClass = normalized(entry->startupWMClass());
        const QString program = normalized(entry->command().value(0));
        const QString programName = normalized(QFileInfo(entry->command().value(0)).fileName());
        const QString icon = normalized(entry->icon());
        const QString name = normalized(entry->name());
        const QString fileName = normalized(QFileInfo(entry->path()).completeBaseName());
        int score = 0;

        if (!wantedAppId.isEmpty() && id == wantedAppId)
            score = qMax(score, 100);
        if (!wantedClass.isEmpty() && id == wantedClass)
            score = qMax(score, 90);
        if (!wantedClass.isEmpty() && startupClass == wantedClass)
            score = qMax(score, 95);
        if (!wantedAppId.isEmpty() && startupClass == wantedAppId)
            score = qMax(score, 95);
        if (!wantedExecutable.isEmpty() && program == wantedExecutable)
            score = qMax(score, 85);
        if (!wantedName.isEmpty() && programName == wantedName)
            score = qMax(score, 80);
        if (!wantedExecutable.isEmpty() && fileName == wantedExecutable)
            score = qMax(score, 75);
        if (!wantedName.isEmpty() && fileName == wantedName)
            score = qMax(score, 75);
        if (!wantedExecutable.isEmpty() && icon == wantedExecutable)
            score = qMax(score, 75);
        if (!wantedName.isEmpty() && icon == wantedName)
            score = qMax(score, 75);
        if (!wantedAppId.isEmpty() && !startupClass.isEmpty()
            && startupClass.startsWith(wantedAppId))
            score = qMax(score, 70);
        if (!wantedClass.isEmpty() && !startupClass.isEmpty()
            && startupClass.startsWith(wantedClass))
            score = qMax(score, 70);
        if (!wantedClass.isEmpty() && id.startsWith(wantedClass))
            score = qMax(score, 60);
        if (!wantedClass.isEmpty() && fileName.startsWith(wantedClass))
            score = qMax(score, 60);
        if (!wantedClass.isEmpty() && program.startsWith(wantedClass))
            score = qMax(score, 55);
        if (!wantedClass.isEmpty() && icon.startsWith(wantedClass))
            score = qMax(score, 55);
        if (!wantedClass.isEmpty() && name.startsWith(wantedClass))
            score = qMax(score, 50);
        if (!wantedExecutable.isEmpty() && !program.isEmpty()
            && wantedExecutable.contains(program))
            score = qMax(score, 45);

        if (score > bestScore) {
            best = entry;
            bestScore = score;
        }
    }

    return best;
}

QList<DesktopEntry *> ApplicationRegistry::entries() const
{
    return m_entries;
}

QList<DesktopEntry *> ApplicationRegistry::applicationEntries() const
{
    return m_model->entries();
}

QStringList ApplicationRegistry::desktopPaths()
{
    QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (dataHome.isEmpty())
        dataHome = QDir::homePath() + QStringLiteral("/.local/share");

    QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty())
        dataDirs = QStringLiteral("/usr/local/share:/usr/share");

    QStringList paths;
    paths.append(QDir::cleanPath(dataHome + QStringLiteral("/applications")));
    for (const QString &dir : dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        paths.append(QDir::cleanPath(dir + QStringLiteral("/applications")));

    paths.removeDuplicates();
    return paths;
}

void ApplicationRegistry::scheduleScan()
{
    m_debounceTimer.start();
}

void ApplicationRegistry::startScan()
{
    if (m_scanInProgress) {
        m_scanQueued = true;
        return;
    }

    m_scanInProgress = true;
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    QThreadPool::globalInstance()->start(new DesktopEntryScanner(this));
}

void ApplicationRegistry::applyScan(const QList<DesktopEntryData> &results)
{
    const bool wasLoading = m_loading;
    m_loading = false;
    m_scanInProgress = false;

    QHash<QString, DesktopEntry *> oldEntries = m_entriesById;
    QHash<QString, DesktopEntry *> newEntries;
    QHash<QString, DesktopEntry *> newEntriesByPath;
    QList<DesktopEntry *> allEntries;
    QList<DesktopEntry *> visibleEntries;
    const QStringList currentDesktops = DesktopEntry::currentDesktops();

    for (const DesktopEntryData &data : results) {
        DesktopEntry *entry = oldEntries.take(data.id);
        if (!entry)
            entry = new DesktopEntry(data.id, this);
        entry->update(data);

        newEntries.insert(data.id, entry);
        newEntriesByPath.insert(QFileInfo(data.path).absoluteFilePath(), entry);
        allEntries.append(entry);

        if (entry->shouldShow(currentDesktops))
            visibleEntries.append(entry);
    }

    std::sort(visibleEntries.begin(), visibleEntries.end(), [](DesktopEntry *a, DesktopEntry *b) {
        const int nameCompare = QString::localeAwareCompare(a->name(), b->name());
        return nameCompare == 0 ? a->id() < b->id() : nameCompare < 0;
    });

    m_entriesById = newEntries;
    m_entriesByPath = newEntriesByPath;
    m_entries = allEntries;
    m_model->setEntries(visibleEntries);
    monitorPaths();

    for (DesktopEntry *entry : std::as_const(oldEntries))
        entry->deleteLater();

    if (wasLoading)
        emit loadingChanged();
    emit applicationsChanged();

    if (m_scanQueued) {
        m_scanQueued = false;
        startScan();
    }
}

void ApplicationRegistry::monitorPaths()
{
    for (const QString &rootPath : desktopPaths()) {
        QDir root(rootPath);
        if (!root.exists()) {
            QString parentPath = rootPath;
            while (!QDir(parentPath).exists()) {
                const QString parent = QFileInfo(parentPath).absolutePath();
                if (parent == parentPath)
                    break;
                parentPath = parent;
            }
            if (QDir(parentPath).exists())
                m_watcher->addPath(parentPath);
            continue;
        }

        m_watcher->addPath(rootPath);
        QDirIterator iterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            m_watcher->addPath(iterator.next());

        QDirIterator files(rootPath, {QStringLiteral("*.desktop")},
                           QDir::Files | QDir::Readable,
                           QDirIterator::Subdirectories);
        while (files.hasNext())
            m_watcher->addPath(files.next());
    }
}

void registerApplicationsQmlTypes()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    qmlRegisterSingletonType<ApplicationRegistry>(
        "Cutefish.Applications", 1, 0, "DesktopEntries",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return ApplicationRegistry::instance();
        });
    qmlRegisterSingletonType<ApplicationRuntime>(
        "Cutefish.Applications", 1, 0, "AppRuntime",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return ApplicationRuntime::instance();
        });
    qmlRegisterUncreatableType<DesktopEntry>(
        "Cutefish.Applications", 1, 0, "DesktopEntry",
        QStringLiteral("DesktopEntry objects are provided by DesktopEntries"));
}
