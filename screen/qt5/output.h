#pragma once

#include <QObject>

class Output
{
    Q_GADGET

public:
    enum Rotation {
        None = 1,
        Left = 2,
        Inverted = 4,
        Right = 8,
    };
    Q_ENUM(Rotation)
};

