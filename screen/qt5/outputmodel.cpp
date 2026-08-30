#include "outputmodel.h"

#include <QGuiApplication>
#include <QtMath>

Qt5OutputModel::Qt5OutputModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int Qt5OutputModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_screens.size();
}

QString Qt5OutputModel::screenName(const QScreen *screen)
{
    if (!screen) {
        return QString();
    }

    const QString name = screen->name();
    return name.isEmpty() ? QStringLiteral("Display") : name;
}

Output::Rotation Qt5OutputModel::rotationForScreen(const QScreen *screen)
{
    if (!screen) {
        return Output::None;
    }

    switch (screen->orientation()) {
    case Qt::PortraitOrientation:
        return Output::Right;
    case Qt::InvertedPortraitOrientation:
        return Output::Left;
    case Qt::InvertedLandscapeOrientation:
        return Output::Inverted;
    case Qt::LandscapeOrientation:
    case Qt::PrimaryOrientation:
    default:
        return Output::None;
    }
}

QVariant Qt5OutputModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_screens.size()) {
        return QVariant();
    }

    const QScreen *screen = m_screens.at(index.row());
    if (!screen) {
        return QVariant();
    }

    switch (role) {
    case Qt::DisplayRole:
        return screenName(screen);
    case EnabledRole:
        return true;
    case InternalRole:
        return screenName(screen).startsWith(QStringLiteral("eDP"), Qt::CaseInsensitive)
            || screenName(screen).startsWith(QStringLiteral("LVDS"), Qt::CaseInsensitive);
    case PrimaryRole:
        return screen == QGuiApplication::primaryScreen();
    case SizeRole:
        return screen->size();
    case PositionRole:
    case NormalizedPositionRole:
        return screen->geometry().topLeft();
    case AutoRotateRole:
    case AutoRotateOnlyInTabletModeRole:
        return false;
    case RotationRole:
        return static_cast<int>(rotationForScreen(screen));
    case ScaleRole:
        return screen->devicePixelRatio();
    case ResolutionIndexRole:
        return 0;
    case ResolutionsRole:
        return QStringList(QStringLiteral("%1 × %2").arg(screen->size().width()).arg(screen->size().height()));
    case RefreshRateIndexRole:
        return 0;
    case RefreshRatesRole:
        return QStringList(QStringLiteral("%1 Hz").arg(qRound(screen->refreshRate())));
    case ReplicationSourceModelRole:
        return QStringList();
    case ReplicationSourceIndexRole:
        return -1;
    case ReplicasModelRole:
        return QVariantList();
    default:
        return QVariant();
    }
}

bool Qt5OutputModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(value)

    if (!index.isValid() || index.row() < 0 || index.row() >= m_screens.size()) {
        return false;
    }

    // QScreen exposes the active mode as read-only. Resolution, rotation and
    // output enablement are intentionally left to the desktop's display
    // service on modern KDE systems.
    switch (role) {
    case EnabledRole:
    case RotationRole:
    case ResolutionIndexRole:
    case RefreshRateIndexRole:
    case ScaleRole:
        return false;
    default:
        return false;
    }
}

QHash<int, QByteArray> Qt5OutputModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[Qt::DisplayRole] = "display";
    roles[EnabledRole] = "enabled";
    roles[InternalRole] = "internal";
    roles[PrimaryRole] = "primary";
    roles[SizeRole] = "size";
    roles[PositionRole] = "position";
    roles[NormalizedPositionRole] = "normalizedPosition";
    roles[AutoRotateRole] = "autoRotate";
    roles[AutoRotateOnlyInTabletModeRole] = "autoRotateOnlyInTabletMode";
    roles[RotationRole] = "rotation";
    roles[ScaleRole] = "scale";
    roles[ResolutionIndexRole] = "resolutionIndex";
    roles[ResolutionsRole] = "resolutions";
    roles[RefreshRateIndexRole] = "refreshRateIndex";
    roles[RefreshRatesRole] = "refreshRates";
    roles[ReplicationSourceModelRole] = "replicationSourceModel";
    roles[ReplicationSourceIndexRole] = "replicationSourceIndex";
    roles[ReplicasModelRole] = "replicasModel";
    return roles;
}

void Qt5OutputModel::setScreens(const QList<QScreen *> &screens)
{
    beginResetModel();
    m_screens = screens;
    endResetModel();
}
