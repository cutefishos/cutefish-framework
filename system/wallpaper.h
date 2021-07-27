#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <QObject>
#include <QDBusInterface>

class Wallpaper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)

public:
    explicit Wallpaper(QObject *parent = nullptr);

    QString path() const;

signals:
    void pathChanged();

private slots:
    void onPathChanged(QString path);

private:
    QDBusInterface m_interface;
    QString m_wallpaper;
};

#endif // WALLPAPER_H
