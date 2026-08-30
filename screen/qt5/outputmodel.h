#pragma once

#include "output.h"

#include <QAbstractListModel>
#include <QScreen>

class Qt5OutputModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum OutputRoles {
        EnabledRole = Qt::UserRole + 1,
        InternalRole,
        PrimaryRole,
        SizeRole,
        PositionRole,
        NormalizedPositionRole,
        AutoRotateRole,
        AutoRotateOnlyInTabletModeRole,
        RotationRole,
        ScaleRole,
        ResolutionIndexRole,
        ResolutionsRole,
        RefreshRateIndexRole,
        RefreshRatesRole,
        ReplicationSourceModelRole,
        ReplicationSourceIndexRole,
        ReplicasModelRole
    };

    explicit Qt5OutputModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index,
                 const QVariant &value,
                 int role = Qt::EditRole) override;

    void setScreens(const QList<QScreen *> &screens);

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    static Output::Rotation rotationForScreen(const QScreen *screen);
    static QString screenName(const QScreen *screen);

    QList<QScreen *> m_screens;
};

