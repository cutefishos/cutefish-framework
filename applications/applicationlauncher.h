#pragma once

#include <QString>
#include <QStringList>

class ApplicationLauncher
{
public:
    static bool startDetached(const QStringList &command,
                              const QString &workingDirectory = QString());
};
