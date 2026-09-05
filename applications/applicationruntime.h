#pragma once

#include <QObject>
#include <QStringList>

class QDBusInterface;

/**
 * Client side of com.cutefish.AppRuntime, the process that owns every
 * application start of the session. Falls back to starting the process in
 * place when the runtime is not available, so callers never have to care.
 */
class ApplicationRuntime : public QObject
{
    Q_OBJECT

public:
    static ApplicationRuntime *instance();

    explicit ApplicationRuntime(QObject *parent = nullptr);

    bool available();

    Q_INVOKABLE bool launchApplication(const QString &appId,
                                       const QStringList &arguments = QStringList());
    Q_INVOKABLE bool launchCommand(const QStringList &command,
                                   const QString &workingDirectory = QString(),
                                   const QString &appId = QString());
    Q_INVOKABLE bool quitApplication(const QString &appId);
    Q_INVOKABLE bool quitAll();
    Q_INVOKABLE bool quitByPid(quint32 pid);
    Q_INVOKABLE bool isRunning(const QString &appId);
    Q_INVOKABLE QStringList runningApplications();
    Q_INVOKABLE QList<quint32> pidsForApplication(const QString &appId);

signals:
    void applicationLaunched(const QString &appId, quint32 pid);
    void applicationQuit(const QString &appId, quint32 pid);

private:
    QDBusInterface *interface();

    QDBusInterface *m_interface = nullptr;
};
