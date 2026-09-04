#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Output
{
    Q_GADGET

public:
    enum Rotation {
        None = 1,
        Left = 2,
        Inverted = 4,
        Right = 8
    };
    Q_ENUM(Rotation)
};

// A Q_GADGET registered directly would become a QML value type, and those have
// to be named in lowercase. Exposing it as a namespace keeps `Output.Left`.
namespace OutputEnums
{
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(Output)
QML_NAMED_ELEMENT(Output)
}
