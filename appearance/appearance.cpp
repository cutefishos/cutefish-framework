#include "appearance.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusServiceWatcher>

namespace
{
constexpr auto Service = "com.cutefish.Services";
constexpr auto ObjectPath = "/com/cutefish/Services/Appearance";
constexpr auto Interface = "com.cutefish.Services.Appearance";
}

Appearance::Appearance(QObject *parent)
    : QObject(parent)
    , m_interface(nullptr)
{
    auto *watcher = new QDBusServiceWatcher(QString::fromLatin1(Service),
                                             QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration,
                                             this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Appearance::init);

    init();
}

void Appearance::init()
{
    delete m_interface;
    m_interface = new QDBusInterface(QString::fromLatin1(Service),
                                     QString::fromLatin1(ObjectPath),
                                     QString::fromLatin1(Interface),
                                     QDBusConnection::sessionBus(),
                                     this);

    if (!m_interface->isValid())
        return;

    connect(m_interface, SIGNAL(darkModeChanged(bool)), this, SLOT(onDarkModeChanged(bool)));
    connect(m_interface, SIGNAL(wallpaperChanged(QString)), this, SLOT(onWallpaperChanged(QString)));
    connect(m_interface, SIGNAL(accentColorChanged(int)), this, SLOT(onAccentColorChanged(int)));
    connect(m_interface, SIGNAL(darkModeDimsWallpaerChanged()), this, SLOT(onDarkModeDimsWallpaperChanged()));
    connect(m_interface, SIGNAL(blurEnabledChanged()), this, SLOT(onBlurEnabledChanged()));
    connect(m_interface, SIGNAL(systemFontPointSizeChanged()), this, SLOT(onFontPointSizeChanged()));
    connect(m_interface, SIGNAL(systemFontChanged()), this, SLOT(onFontFamilyChanged()));
    connect(m_interface, SIGNAL(backgroundTypeChanged()), this, SLOT(onBackgroundTypeChanged()));
    connect(m_interface, SIGNAL(backgroundColorChanged()), this, SLOT(onBackgroundColorChanged()));
    connect(m_interface, SIGNAL(backgroundVisibleChanged()), this, SLOT(onBackgroundVisibleChanged()));

    emit darkModeChanged();
    emit wallpaperChanged();
    emit accentColorIndexChanged();
    emit dimsWallpaperChanged();
    emit blurEnabledChanged();
    emit fontPointSizeChanged();
    emit fontFamilyChanged();
    emit fixedFontFamilyChanged();
    emit backgroundTypeChanged();
    emit backgroundColorChanged();
    emit backgroundVisibleChanged();
}

QVariant Appearance::serviceProperty(const char *name) const
{
    return m_interface && m_interface->isValid() ? m_interface->property(name) : QVariant();
}

void Appearance::callService(const char *method)
{
    if (m_interface && m_interface->isValid())
        m_interface->call(method);
}

void Appearance::callService(const char *method, const QVariant &value)
{
    if (m_interface && m_interface->isValid())
        m_interface->call(method, value);
}

bool Appearance::darkMode() const
{
    return serviceProperty("isDarkMode").toBool();
}

void Appearance::switchDarkMode(bool darkMode)
{
    callService("setDarkMode", darkMode);
}

bool Appearance::dimsWallpaper() const
{
    return serviceProperty("darkModeDimsWallpaer").toBool();
}

void Appearance::setDimsWallpaper(bool value)
{
    callService("setDarkModeDimsWallpaer", value);
}

bool Appearance::blurEnabled() const
{
    return serviceProperty("blurEnabled").toBool();
}

void Appearance::setBlurEnabled(bool value)
{
    callService("setBlurEnabled", value);
}

int Appearance::accentColorIndex() const
{
    return serviceProperty("accentColor").toInt();
}

void Appearance::setAccentColor(int accentColor)
{
    callService("setAccentColor", accentColor);
}

qreal Appearance::fontPointSize() const
{
    return serviceProperty("systemFontPointSize").toReal();
}

void Appearance::setFontPointSize(qreal fontPointSize)
{
    callService("setSystemFontPointSize", fontPointSize);
}

QString Appearance::fontFamily() const
{
    return serviceProperty("systemFont").toString();
}

void Appearance::setFontFamily(const QString &name)
{
    if (!name.isEmpty())
        callService("setSystemFont", name);
}

QString Appearance::fixedFontFamily() const
{
    return serviceProperty("systemFixedFont").toString();
}

// com.cutefish.Services.Appearance has no systemFixedFontChanged signal, so
// the change is announced here.
void Appearance::setFixedFontFamily(const QString &name)
{
    if (!name.isEmpty()) {
        callService("setSystemFixedFont", name);
        emit fixedFontFamilyChanged();
    }
}

QString Appearance::wallpaper() const
{
    return serviceProperty("wallpaper").toString();
}

void Appearance::setWallpaper(const QString &path)
{
    if (!path.isEmpty())
        callService("setWallpaper", path);
}

int Appearance::backgroundType() const
{
    return serviceProperty("backgroundType").toInt();
}

void Appearance::setBackgroundType(int type)
{
    callService("setBackgroundType", type);
}

QString Appearance::backgroundColor() const
{
    return serviceProperty("backgroundColor").toString();
}

void Appearance::setBackgroundColor(const QString &color)
{
    callService("setBackgroundColor", color);
}

bool Appearance::backgroundVisible() const
{
    return serviceProperty("backgroundVisible").toBool();
}

void Appearance::setCursorTheme(const QString &theme)
{
    if (!theme.isEmpty())
        callService("setCursorTheme", theme);
}

void Appearance::applyFontSettings()
{
    callService("applyFontSettings");
}

void Appearance::onDarkModeChanged(bool darkMode)
{
    Q_UNUSED(darkMode);
    emit darkModeChanged();
}

void Appearance::onWallpaperChanged(const QString &path)
{
    Q_UNUSED(path);
    emit wallpaperChanged();
}

void Appearance::onAccentColorChanged(int accentColor)
{
    Q_UNUSED(accentColor);
    emit accentColorIndexChanged();
}

void Appearance::onDarkModeDimsWallpaperChanged()
{
    emit dimsWallpaperChanged();
}

void Appearance::onBlurEnabledChanged()
{
    emit blurEnabledChanged();
}

void Appearance::onFontPointSizeChanged()
{
    emit fontPointSizeChanged();
}

void Appearance::onFontFamilyChanged()
{
    emit fontFamilyChanged();
}

void Appearance::onBackgroundTypeChanged()
{
    emit backgroundTypeChanged();
}

void Appearance::onBackgroundColorChanged()
{
    emit backgroundColorChanged();
}

void Appearance::onBackgroundVisibleChanged()
{
    emit backgroundVisibleChanged();
}
