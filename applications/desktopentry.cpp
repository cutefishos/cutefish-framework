#include "desktopentry.h"

#include "applicationlauncher.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>

namespace {

QStringList splitList(const QString &value)
{
    QStringList result;
    for (const QString &item : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        result.append(item.trimmed());
    return result;
}

QString unescapeValue(QString value)
{
    value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
    value.replace(QStringLiteral("\\r"), QStringLiteral("\r"));
    value.replace(QStringLiteral("\\s"), QStringLiteral(" "));
    value.replace(QStringLiteral("\\;"), QStringLiteral(";"));
    value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return value;
}

bool parseBool(const QString &value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

QString localizedValue(const QMap<QString, QString> &values,
                       const QMap<QString, QString> &localized,
                       const QString &key)
{
    const QLocale locale = QLocale::system();
    const QStringList candidates = {
        locale.name(),
        locale.bcp47Name(),
        locale.name().section(QLatin1Char('_'), 0, 0)
    };

    for (const QString &candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        const auto it = localized.constFind(key + QLatin1Char('[') + candidate
                                            + QLatin1Char(']'));
        if (it != localized.constEnd())
            return *it;
    }

    return values.value(key);
}

QStringList tokenizeExec(const QString &exec)
{
    QStringList result;
    QString token;
    bool quoted = false;
    QChar quote;

    for (int i = 0; i < exec.size(); ++i) {
        const QChar ch = exec.at(i);

        if (ch == QLatin1Char('\\') && i + 1 < exec.size()) {
            token.append(exec.at(++i));
            continue;
        }

        if ((ch == QLatin1Char('"') || ch == QLatin1Char('\''))) {
            if (!quoted) {
                quoted = true;
                quote = ch;
            } else if (quote == ch) {
                quoted = false;
            } else {
                token.append(ch);
            }
            continue;
        }

        if (!quoted && ch.isSpace()) {
            if (!token.isEmpty()) {
                result.append(token);
                token.clear();
            }
            continue;
        }

        token.append(ch);
    }

    if (!token.isEmpty())
        result.append(token);

    return result;
}

QString substituteToken(const QString &token, const DesktopEntry *entry,
                        const QStringList &arguments)
{
    QString result;

    for (int i = 0; i < token.size(); ++i) {
        if (token.at(i) != QLatin1Char('%') || i + 1 >= token.size()) {
            result.append(token.at(i));
            continue;
        }

        const QChar field = token.at(++i);
        if (field == QLatin1Char('%')) {
            result.append(QLatin1Char('%'));
        } else if (field == QLatin1Char('c')) {
            result.append(entry->name());
        } else if (field == QLatin1Char('k')) {
            result.append(entry->path());
        } else if (field == QLatin1Char('i')) {
            // %i is handled as two arguments by commandForArguments().
        } else if (field == QLatin1Char('f') || field == QLatin1Char('u')) {
            if (!arguments.isEmpty())
                result.append(arguments.first());
        } else if (field == QLatin1Char('F') || field == QLatin1Char('U')) {
            if (!arguments.isEmpty())
                result.append(arguments.first());
        }
    }

    return result;
}

} // namespace

DesktopEntry::DesktopEntry(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
{
}

QString DesktopEntry::id() const { return m_id; }
QString DesktopEntry::path() const { return m_path; }
QString DesktopEntry::name() const { return m_name; }
QString DesktopEntry::genericName() const { return m_genericName; }
QString DesktopEntry::comment() const { return m_comment; }
QString DesktopEntry::icon() const { return m_icon; }
QString DesktopEntry::exec() const { return m_exec; }
QStringList DesktopEntry::command() const { return m_command; }
QString DesktopEntry::workingDirectory() const { return m_workingDirectory; }
QString DesktopEntry::startupWMClass() const { return m_startupWMClass; }
QStringList DesktopEntry::categories() const { return m_categories; }
QStringList DesktopEntry::keywords() const { return m_keywords; }
QStringList DesktopEntry::mimeTypes() const { return m_mimeTypes; }
bool DesktopEntry::terminal() const { return m_terminal; }
bool DesktopEntry::noDisplay() const { return m_noDisplay; }
bool DesktopEntry::hidden() const { return m_hidden; }

QStringList DesktopEntry::commandForArguments(const QStringList &arguments) const
{
    QStringList result;
    const QStringList tokens = tokenizeExec(m_exec);

    for (const QString &token : tokens) {
        if (token == QStringLiteral("%F") || token == QStringLiteral("%U")) {
            result.append(arguments);
            continue;
        }

        if (token == QStringLiteral("%f") || token == QStringLiteral("%u")) {
            if (!arguments.isEmpty())
                result.append(arguments.first());
            continue;
        }

        if (token == QStringLiteral("%i")) {
            if (!m_icon.isEmpty())
                result << QStringLiteral("--icon") << m_icon;
            continue;
        }

        const QString substituted = substituteToken(token, this, arguments);
        if (!substituted.isEmpty())
            result.append(substituted);
    }

    return result;
}

bool DesktopEntry::launch(const QStringList &arguments) const
{
    return ApplicationLauncher::startDetached(commandForArguments(arguments),
                                               m_workingDirectory);
}

QStringList DesktopEntry::parseExec(const QString &exec)
{
    // This intentionally returns a command without field-code arguments. The
    // complete expansion is available through commandForArguments().
    DesktopEntry entry(QStringLiteral("temporary"));
    entry.m_exec = exec;
    return entry.commandForArguments(QStringList());
}

bool DesktopEntry::parse(const QString &id, const QString &path,
                         const QByteArray &contents, DesktopEntryData *result)
{
    if (!result)
        return false;

    DesktopEntryData data;
    data.id = id;
    data.path = path;

    QString group;
    QMap<QString, QString> values;
    QMap<QString, QString> localized;
    QMap<QString, QMap<QString, QString>> actionValues;

    auto finishGroup = [&]() {
        if (group == QStringLiteral("Desktop Entry")) {
            data.type = values.value(QStringLiteral("Type"));
            data.name = localizedValue(values, localized, QStringLiteral("Name"));
            data.genericName = localizedValue(values, localized, QStringLiteral("GenericName"));
            data.comment = localizedValue(values, localized, QStringLiteral("Comment"));
            data.icon = values.value(QStringLiteral("Icon"));
            data.exec = values.value(QStringLiteral("Exec"));
            data.workingDirectory = values.value(QStringLiteral("Path"));
            data.startupWMClass = values.value(QStringLiteral("StartupWMClass"));
            data.categories = splitList(values.value(QStringLiteral("Categories")));
            data.keywords = splitList(values.value(QStringLiteral("Keywords")));
            data.mimeTypes = splitList(values.value(QStringLiteral("MimeType")));
            data.onlyShowIn = splitList(values.value(QStringLiteral("OnlyShowIn")));
            data.notShowIn = splitList(values.value(QStringLiteral("NotShowIn")));
            data.terminal = parseBool(values.value(QStringLiteral("Terminal")));
            data.noDisplay = parseBool(values.value(QStringLiteral("NoDisplay")));
            data.hidden = parseBool(values.value(QStringLiteral("Hidden")));
        } else if (group.startsWith(QStringLiteral("Desktop Action "))) {
            const QString actionId = group.mid(QStringLiteral("Desktop Action ").size());
            const auto action = actionValues.value(actionId);
            if (!action.isEmpty()) {
                DesktopActionData item;
                item.id = actionId;
                item.name = action.value(QStringLiteral("Name"));
                item.icon = action.value(QStringLiteral("Icon"));
                item.exec = action.value(QStringLiteral("Exec"));
                data.actions.append(item);
            }
        }

        values.clear();
        localized.clear();
    };

    const QStringList lines = QString::fromUtf8(contents).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            finishGroup();
            group = line.mid(1, line.size() - 2);
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        QString key = line.left(equals).trimmed();
        const QString value = unescapeValue(line.mid(equals + 1).trimmed());
        if (group.startsWith(QStringLiteral("Desktop Action "))) {
            actionValues[group.mid(QStringLiteral("Desktop Action ").size())][key] = value;
        } else if (group == QStringLiteral("Desktop Entry")) {
            const int localeStart = key.indexOf(QLatin1Char('['));
            if (localeStart > 0 && key.endsWith(QLatin1Char(']'))) {
                localized.insert(key, value);
                key = key.left(localeStart);
            } else {
                values.insert(key, value);
            }
        }
    }
    finishGroup();

    if (data.type != QStringLiteral("Application") || data.name.isEmpty())
        return false;

    *result = data;
    return true;
}

void DesktopEntry::update(const DesktopEntryData &data)
{
    if (m_path != data.path) {
        m_path = data.path;
        emit pathChanged();
    }
    if (m_name != data.name) {
        m_name = data.name;
        emit nameChanged();
    }
    if (m_genericName != data.genericName) {
        m_genericName = data.genericName;
        emit genericNameChanged();
    }
    if (m_comment != data.comment) {
        m_comment = data.comment;
        emit commentChanged();
    }
    if (m_icon != data.icon) {
        m_icon = data.icon;
        emit iconChanged();
    }
    const bool hasExecChanged = m_exec != data.exec;
    if (hasExecChanged) {
        m_exec = data.exec;
        emit execChanged();
    }
    if (m_workingDirectory != data.workingDirectory) {
        m_workingDirectory = data.workingDirectory;
        emit workingDirectoryChanged();
    }
    if (m_startupWMClass != data.startupWMClass) {
        m_startupWMClass = data.startupWMClass;
        emit startupWMClassChanged();
    }
    if (m_categories != data.categories) {
        m_categories = data.categories;
        emit categoriesChanged();
    }
    if (m_keywords != data.keywords) {
        m_keywords = data.keywords;
        emit keywordsChanged();
    }
    if (m_mimeTypes != data.mimeTypes) {
        m_mimeTypes = data.mimeTypes;
        emit mimeTypesChanged();
    }
    if (m_terminal != data.terminal) {
        m_terminal = data.terminal;
        emit terminalChanged();
    }
    if (m_noDisplay != data.noDisplay) {
        m_noDisplay = data.noDisplay;
        emit noDisplayChanged();
    }
    if (m_hidden != data.hidden) {
        m_hidden = data.hidden;
        emit hiddenChanged();
    }

    const QStringList command = commandForArguments(QStringList());
    if (m_command != command) {
        m_command = command;
        emit commandChanged();
    }

    emit changed();
}
