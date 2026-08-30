#include "screen.h"

#include "outputmodel.h"

#include <QGuiApplication>

Qt5Screen::Qt5Screen(QObject *parent)
    : QObject(parent)
    , m_outputModel(new Qt5OutputModel(this))
{
    refreshScreens();

    connect(qApp, &QGuiApplication::screenAdded, this, &Qt5Screen::connectScreen);
    connect(qApp, &QGuiApplication::screenAdded, this, &Qt5Screen::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &Qt5Screen::refreshScreens);
}

Qt5OutputModel *Qt5Screen::outputModel() const
{
    return m_outputModel;
}

void Qt5Screen::connectScreen(QScreen *screen)
{
    if (!screen) {
        return;
    }

    connect(screen, &QScreen::geometryChanged, this, &Qt5Screen::refreshScreens, Qt::UniqueConnection);
    connect(screen, &QScreen::orientationChanged, this, &Qt5Screen::refreshScreens, Qt::UniqueConnection);
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, &Qt5Screen::refreshScreens, Qt::UniqueConnection);
}

void Qt5Screen::refreshScreens()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        connectScreen(screen);
    }
    m_outputModel->setScreens(screens);
    emit outputModelChanged();
}

void Qt5Screen::save()
{
    // Display configuration is managed by KScreen/system settings on KDE6.
    // QScreen is a read-only Qt API, so there is nothing to write here.
}

