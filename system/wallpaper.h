#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <QObject>
#include <QDBusInterface>

/**
 * The desktop background as cutefish-services publishes it over
 * com.cutefish.Services.Appearance: an image (type 0) or a plain colour (type 1).
 */
class Wallpaper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int type READ type NOTIFY typeChanged)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool dimsWallpaper READ dimsWallpaper NOTIFY dimsWallpaperChanged)
    Q_PROPERTY(QString color READ color NOTIFY colorChanged)
    Q_PROPERTY(bool backgroundVisible READ backgroundVisible NOTIFY backgroundVisibleChanged)

public:
    explicit Wallpaper(QObject *parent = nullptr);

    int type() const;

    QString path() const;
    bool dimsWallpaper() const;

    QString color() const;
    bool backgroundVisible() const;

signals:
    void pathChanged();
    void dimsWallpaperChanged();
    void typeChanged();
    void colorChanged();
    void backgroundVisibleChanged();

private:
    QVariant themeProperty(const char *name) const;

private slots:
    void init();
    void onPathChanged(QString path);

private:
    // Recreated in init(): a QDBusInterface built while cutefish-services is
    // not on the bus caches a failed introspection and never recovers.
    QDBusInterface *m_interface;
};

#endif // WALLPAPER_H
