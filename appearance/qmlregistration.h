#ifndef CUTEFISH_APPEARANCE_QMLREGISTRATION_H
#define CUTEFISH_APPEARANCE_QMLREGISTRATION_H

#include <QtQml/qqmlregistration.h>

#include "appearance.h"
#include "wallpaper.h"

// The types themselves live in libcutefish-framework-appearance, which settings
// and FishUI link against. Registering Cutefish.Appearance from a separate
// plugin keeps the module registration in one library: a process that both
// links the library and imports the module would otherwise load two copies of
// the same .so, and Qt refuses the second registration of the URI.

struct AppearanceForeign
{
    Q_GADGET
    QML_FOREIGN(Appearance)
    QML_NAMED_ELEMENT(Appearance)
};

struct WallpaperForeign
{
    Q_GADGET
    QML_FOREIGN(Wallpaper)
    QML_NAMED_ELEMENT(Wallpaper)
};

#endif // CUTEFISH_APPEARANCE_QMLREGISTRATION_H
