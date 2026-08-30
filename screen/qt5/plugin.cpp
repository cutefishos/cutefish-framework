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
        qmlRegisterUncreatableMetaObject(Output::staticMetaObject, uri, 1, 0, "Output",
                                         QStringLiteral("Output is only a namespace for enums"));
    }
};

#include "plugin.moc"

