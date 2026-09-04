#include "wallpaper.h"

#include <QDir>
#include <QDirIterator>

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
    QStringList result;
    QDirIterator iterator(QStringLiteral("/usr/share/backgrounds/cutefishos"),
                          {QStringLiteral("*.jpg"), QStringLiteral("*.png")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
        result.append(iterator.next());

    result.sort();
    return result;
}
