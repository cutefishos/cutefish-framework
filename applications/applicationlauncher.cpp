#include "applicationlauncher.h"
#include "applicationruntime.h"

bool ApplicationLauncher::startDetached(const QStringList &command,
                                        const QString &workingDirectory,
                                        const QString &appId)
{
    // Every application start of the session goes through the runtime, which
    // falls back to starting the process here when it is not running.
    return ApplicationRuntime::instance()->launchCommand(command, workingDirectory, appId);
}

bool ApplicationLauncher::quit(const QString &appId)
{
    return ApplicationRuntime::instance()->quitApplication(appId);
}
