#include "applicationruntime.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>

static const char *kService = "com.cutefish.AppRuntime";
static const char *kPath = "/AppRuntime";

ApplicationRuntime *ApplicationRuntime::instance()
{
    static ApplicationRuntime *runtime = new ApplicationRuntime;
    return runtime;
}

ApplicationRuntime::ApplicationRuntime(QObject *parent)
    : QObject(parent)
{
}

QDBusInterface *ApplicationRuntime::interface()
{
    if (!m_interface) {
        m_interface = new QDBusInterface(QLatin1String(kService), QLatin1String(kPath),
                                         QLatin1String(kService),
                                         QDBusConnection::sessionBus(), this);
        // Launching is a blocking call from user interface code, so a runtime
        // that stops answering must not freeze the caller for the default
        // half minute.
        m_interface->setTimeout(5000);
        QDBusConnection::sessionBus().connect(QLatin1String(kService), QLatin1String(kPath),
                                              QLatin1String(kService),
                                              QStringLiteral("applicationLaunched"), this,
                                              SIGNAL(applicationLaunched(QString, quint32)));
        QDBusConnection::sessionBus().connect(QLatin1String(kService), QLatin1String(kPath),
                                              QLatin1String(kService),
                                              QStringLiteral("applicationQuit"), this,
                                              SIGNAL(applicationQuit(QString, quint32)));
    }

    return m_interface;
}

bool ApplicationRuntime::available()
{
    return interface()->isValid();
}

bool ApplicationRuntime::launchApplication(const QString &appId, const QStringList &arguments)
{
    if (appId.isEmpty())
        return false;

    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return false;

    const QDBusReply<uint> reply = iface->call(QStringLiteral("launchApplication"), appId, arguments);

    return reply.isValid() && reply.value() != 0;
}

bool ApplicationRuntime::launchCommand(const QStringList &command,
                                       const QString &workingDirectory,
                                       const QString &appId)
{
    if (command.isEmpty() || command.first().isEmpty())
        return false;

    QDBusInterface *iface = interface();
    if (iface->isValid()) {
        const QDBusReply<uint> reply = iface->call(QStringLiteral("launchCommand"), appId,
                                                   command, workingDirectory);
        if (reply.isValid() && reply.value() != 0)
            return true;
    }

    return QProcess::startDetached(command.first(), command.mid(1), workingDirectory);
}

bool ApplicationRuntime::quitApplication(const QString &appId)
{
    if (appId.isEmpty())
        return false;

    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return false;

    const QDBusReply<bool> reply = iface->call(QStringLiteral("quitApplication"), appId);

    return reply.isValid() && reply.value();
}

bool ApplicationRuntime::quitAll()
{
    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return false;

    const QDBusReply<bool> reply = iface->call(QStringLiteral("quitAll"));

    return reply.isValid() && reply.value();
}

bool ApplicationRuntime::quitByPid(quint32 pid)
{
    if (pid == 0)
        return false;

    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return false;

    const QDBusReply<bool> reply = iface->call(QStringLiteral("quitByPid"), pid);

    return reply.isValid() && reply.value();
}

bool ApplicationRuntime::isRunning(const QString &appId)
{
    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return false;

    const QDBusReply<bool> reply = iface->call(QStringLiteral("isRunning"), appId);

    return reply.isValid() && reply.value();
}

QStringList ApplicationRuntime::runningApplications()
{
    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return QStringList();

    const QDBusReply<QStringList> reply = iface->call(QStringLiteral("runningApplications"));

    return reply.isValid() ? reply.value() : QStringList();
}

QList<quint32> ApplicationRuntime::pidsForApplication(const QString &appId)
{
    QDBusInterface *iface = interface();
    if (!iface->isValid())
        return QList<quint32>();

    const QDBusReply<QList<uint>> reply = iface->call(QStringLiteral("pidsForApplication"), appId);

    return reply.isValid() ? reply.value() : QList<quint32>();
}
