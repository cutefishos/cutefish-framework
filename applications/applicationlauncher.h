#pragma once

#include <QString>
#include <QStringList>

class ApplicationLauncher
{
public:
    // appId is only a hint for the runtime: it is the key an instance is
    // tracked and can later be quit under.
    static bool startDetached(const QStringList &command,
                              const QString &workingDirectory = QString(),
                              const QString &appId = QString());

    static bool quit(const QString &appId);
};
