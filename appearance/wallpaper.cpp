#include "wallpaper.h"

#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>

Wallpaper::Wallpaper(QObject *parent)
    : Appearance(parent)
{
    connect(this, &Appearance::backgroundTypeChanged, this, &Wallpaper::typeChanged);
    connect(this, &Appearance::wallpaperChanged, this, &Wallpaper::pathChanged);
    connect(this, &Appearance::backgroundColorChanged, this, &Wallpaper::colorChanged);
}

int Wallpaper::type() const
{
    return backgroundType();
}

void Wallpaper::setType(int type)
{
    setBackgroundType(type);
}

QString Wallpaper::path() const
{
    return wallpaper();
}

void Wallpaper::setPath(const QString &path)
{
    setWallpaper(path);
}

QString Wallpaper::color() const
{
    return backgroundColor();
}

void Wallpaper::setColor(const QString &color)
{
    setBackgroundColor(color);
}

QStringList Wallpaper::backgrounds() const
{
    // Every XDG data directory, so a user's own wallpapers in
    // ~/.local/share/backgrounds/cutefishos show up next to the shipped ones.
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                       QStringLiteral("backgrounds/cutefishos"),
                                                       QStandardPaths::LocateDirectory);

    QStringList result;
    for (const QString &dir : dirs) {
        QDirIterator iterator(dir,
                              {QStringLiteral("*.jpg"), QStringLiteral("*.png")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            result.append(iterator.next());
    }

    result.removeDuplicates();
    result.sort();
    return result;
}
