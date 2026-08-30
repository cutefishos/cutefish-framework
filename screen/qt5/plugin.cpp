#include "output.h"
#include "screen.h"

#include <QQmlExtensionPlugin>
#include <qqml.h>

class QmlPlugins : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")

public:
    void registerTypes(const char *uri) override
    {
        qmlRegisterType<Qt5Screen>(uri, 1, 0, "Screen");
        qmlRegisterUncreatableType<Output>(uri, 1, 0, "Output",
                                           QStringLiteral("Output is provided by Screen"));
    }
};

#include "plugin.moc"

