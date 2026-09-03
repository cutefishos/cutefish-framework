#include "wallpaper.h"

#include <QDBusServiceWatcher>

Wallpaper::Wallpaper(QObject *parent)
    : QObject(parent)
    , m_interface(nullptr)
{
    // The shell starts before cutefish-settings, so the signals are hooked up
    // again once the service appears; without this the desktop would keep the
    // background it read at startup for the rest of the session.
    QDBusServiceWatcher *watcher = new QDBusServiceWatcher(this);
    watcher->setConnection(QDBusConnection::sessionBus());
    watcher->addWatchedService("com.cutefish.Settings");
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Wallpaper::init);

    init();
}

void Wallpaper::init()
{
    delete m_interface;
    m_interface = new QDBusInterface("com.cutefish.Settings",
                                     "/Theme", "com.cutefish.Theme",
                                     QDBusConnection::sessionBus(), this);

    if (!m_interface->isValid())
        return;

    connect(m_interface, SIGNAL(wallpaperChanged(QString)), this, SLOT(onPathChanged(QString)));
    connect(m_interface, SIGNAL(darkModeDimsWallpaerChanged()), this, SIGNAL(dimsWallpaperChanged()));
    connect(m_interface, SIGNAL(backgroundTypeChanged()), this, SIGNAL(typeChanged()));
    connect(m_interface, SIGNAL(backgroundColorChanged()), this, SIGNAL(colorChanged()));
    connect(m_interface, SIGNAL(backgroundVisibleChanged()), this, SIGNAL(backgroundVisibleChanged()));

    emit typeChanged();
    emit pathChanged();
    emit colorChanged();
    emit dimsWallpaperChanged();
    emit backgroundVisibleChanged();
}

QVariant Wallpaper::themeProperty(const char *name) const
{
    return m_interface ? m_interface->property(name) : QVariant();
}

int Wallpaper::type() const
{
    return themeProperty("backgroundType").toInt();
}

QString Wallpaper::path() const
{
    return themeProperty("wallpaper").toString();
}

bool Wallpaper::dimsWallpaper() const
{
    return themeProperty("darkModeDimsWallpaer").toBool();
}

QString Wallpaper::color() const
{
    return themeProperty("backgroundColor").toString();
}

bool Wallpaper::backgroundVisible() const
{
    return themeProperty("backgroundVisible").toBool();
}

void Wallpaper::onPathChanged(QString path)
{
    Q_UNUSED(path);

    emit pathChanged();
}
