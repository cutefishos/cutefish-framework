#ifndef WALLPAPER_H
#define WALLPAPER_H

#include "appearance.h"

#include <QStringList>

/**
 * The desktop background as cutefish-services publishes it over
 * com.cutefish.Services.Appearance: an image (type 0) or a plain colour (type 1).
 */
class Wallpaper : public Appearance
{
    Q_OBJECT
    Q_PROPERTY(int type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QStringList backgrounds READ backgrounds CONSTANT)

public:
    explicit Wallpaper(QObject *parent = nullptr);

    int type() const;
    Q_INVOKABLE void setType(int type);

    QString path() const;
    Q_INVOKABLE void setPath(const QString &path);

    QString color() const;
    Q_INVOKABLE void setColor(const QString &color);

    QStringList backgrounds() const;

signals:
    void pathChanged();
    void typeChanged();
    void colorChanged();
};

#endif // WALLPAPER_H
