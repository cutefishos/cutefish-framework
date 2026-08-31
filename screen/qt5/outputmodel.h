#pragma once

#include "output.h"

#include <KScreen/Config>

#include <QAbstractListModel>

class KScreenOutputModel : public QAbstractListModel
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

    explicit KScreenOutputModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index,
                 const QVariant &value,
                 int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setConfig(const KScreen::ConfigPtr &config);

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    struct ModeChoice {
        QString id;
        QSize size;
        float refreshRate = 0;
    };

    KScreen::OutputPtr outputAt(int row) const;
    QList<ModeChoice> modesForOutput(const KScreen::OutputPtr &output) const;
    QList<QSize> resolutionsForOutput(const KScreen::OutputPtr &output) const;
    QList<float> refreshRatesForOutput(const KScreen::OutputPtr &output,
                                       const QSize &size) const;

    static QString outputName(const KScreen::OutputPtr &output);
    static QString formatResolution(const QSize &size);
    static QString formatRefreshRate(float refreshRate);
    static bool sameRefreshRate(float first, float second);

    KScreen::ConfigPtr m_config;
    QList<KScreen::OutputPtr> m_outputs;
};
