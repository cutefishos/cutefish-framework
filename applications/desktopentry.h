#pragma once

#include <QMetaType>
#include <QObject>
#include <QStringList>

struct DesktopActionData
{
    QString id;
    QString name;
    QString icon;
    QString exec;
};

struct DesktopEntryData
{
    QString id;
    QString path;
    QString type;
    QString name;
    QString genericName;
    QString comment;
    QString icon;
    QString exec;
    QString workingDirectory;
    QString startupWMClass;
    QStringList categories;
    QStringList keywords;
    QStringList mimeTypes;
    QStringList onlyShowIn;
    QStringList notShowIn;
    QList<DesktopActionData> actions;
    bool terminal = false;
    bool noDisplay = false;
    bool hidden = false;
};

Q_DECLARE_METATYPE(DesktopEntryData)
Q_DECLARE_METATYPE(QList<DesktopEntryData>)

class DesktopEntry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString genericName READ genericName NOTIFY genericNameChanged)
    Q_PROPERTY(QString comment READ comment NOTIFY commentChanged)
    Q_PROPERTY(QString icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(QString exec READ exec NOTIFY execChanged)
    Q_PROPERTY(QStringList command READ command NOTIFY commandChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(QString startupWMClass READ startupWMClass NOTIFY startupWMClassChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QStringList keywords READ keywords NOTIFY keywordsChanged)
    Q_PROPERTY(QStringList mimeTypes READ mimeTypes NOTIFY mimeTypesChanged)
    Q_PROPERTY(bool terminal READ terminal NOTIFY terminalChanged)
    Q_PROPERTY(bool noDisplay READ noDisplay NOTIFY noDisplayChanged)
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)

public:
    explicit DesktopEntry(const QString &id, QObject *parent = nullptr);

    QString id() const;
    QString path() const;
    QString name() const;
    QString genericName() const;
    QString comment() const;
    QString icon() const;
    QString exec() const;
    QStringList command() const;
    QString workingDirectory() const;
    QString startupWMClass() const;
    QStringList categories() const;
    QStringList keywords() const;
    QStringList mimeTypes() const;
    bool terminal() const;
    bool noDisplay() const;
    bool hidden() const;

    Q_INVOKABLE bool launch(const QStringList &arguments = QStringList()) const;

    QStringList commandForArguments(const QStringList &arguments) const;

    static bool parse(const QString &id, const QString &path,
                      const QByteArray &contents, DesktopEntryData *result);
    static QStringList parseExec(const QString &exec);

    void update(const DesktopEntryData &data);

signals:
    void pathChanged();
    void nameChanged();
    void genericNameChanged();
    void commentChanged();
    void iconChanged();
    void execChanged();
    void commandChanged();
    void workingDirectoryChanged();
    void startupWMClassChanged();
    void categoriesChanged();
    void keywordsChanged();
    void mimeTypesChanged();
    void terminalChanged();
    void noDisplayChanged();
    void hiddenChanged();
    void changed();

private:
    QString m_id;
    QString m_path;
    QString m_name;
    QString m_genericName;
    QString m_comment;
    QString m_icon;
    QString m_exec;
    QStringList m_command;
    QString m_workingDirectory;
    QString m_startupWMClass;
    QStringList m_categories;
    QStringList m_keywords;
    QStringList m_mimeTypes;
    bool m_terminal = false;
    bool m_noDisplay = false;
    bool m_hidden = false;
};
