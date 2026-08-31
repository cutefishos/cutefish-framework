#include "applicationlauncher.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QProcess>

bool ApplicationLauncher::startDetached(const QStringList &command,
                                        const QString &workingDirectory)
{
    if (command.isEmpty() || command.first().isEmpty())
        return false;

    const QString program = command.first();
    const QStringList arguments = command.mid(1);

    // Keep using the session launch service when it is available. It applies
    // the desktop session's environment and startup handling consistently.
    QDBusInterface session("com.cutefish.Session", "/Session",
                           "com.cutefish.Session", QDBusConnection::sessionBus());
    if (session.isValid()) {
        session.asyncCall("launch", program, arguments);
        return true;
    }

    return QProcess::startDetached(program, arguments, workingDirectory);
}
