#include "outputmodel.h"

#include <KScreen/Mode>
#include <KScreen/Output>

#include <algorithm>
#include <limits>

#include <QtMath>

KScreenOutputModel::KScreenOutputModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int KScreenOutputModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_outputs.size();
}

KScreen::OutputPtr KScreenOutputModel::outputAt(int row) const
{
    if (row < 0 || row >= m_outputs.size()) {
        return {};
    }

    return m_outputs.at(row);
}

QList<KScreenOutputModel::ModeChoice> KScreenOutputModel::modesForOutput(const KScreen::OutputPtr &output) const
{
    QList<ModeChoice> modes;
    if (!output) {
        return modes;
    }

    const KScreen::ModeList outputModes = output->modes();
    for (auto it = outputModes.cbegin(); it != outputModes.cend(); ++it) {
        if (!it.value()) {
            continue;
        }

        modes.append({it.key(), it.value()->size(), it.value()->refreshRate()});
    }
    return modes;
}

QList<QSize> KScreenOutputModel::resolutionsForOutput(const KScreen::OutputPtr &output) const
{
    QList<QSize> resolutions;
    for (const ModeChoice &mode : modesForOutput(output)) {
        if (mode.size.isValid() && !resolutions.contains(mode.size)) {
            resolutions.append(mode.size);
        }
    }
    std::sort(resolutions.begin(), resolutions.end(), [](const QSize &first, const QSize &second) {
        const qint64 firstArea = qint64(first.width()) * first.height();
        const qint64 secondArea = qint64(second.width()) * second.height();
        if (firstArea != secondArea) {
            return firstArea > secondArea;
        }
        return first.width() > second.width();
    });
    return resolutions;
}

QList<float> KScreenOutputModel::refreshRatesForOutput(const KScreen::OutputPtr &output,
                                                       const QSize &size) const
{
    QList<float> refreshRates;
    for (const ModeChoice &mode : modesForOutput(output)) {
        if (mode.size != size || mode.refreshRate <= 0) {
            continue;
        }

        bool alreadyListed = false;
        for (const float refreshRate : refreshRates) {
            if (sameRefreshRate(refreshRate, mode.refreshRate)) {
                alreadyListed = true;
                break;
            }
        }
        if (!alreadyListed) {
            refreshRates.append(mode.refreshRate);
        }
    }
    std::sort(refreshRates.begin(), refreshRates.end());
    return refreshRates;
}

QString KScreenOutputModel::outputName(const KScreen::OutputPtr &output)
{
    if (!output) {
        return QString();
    }

    if (!output->name().isEmpty()) {
        return output->name();
    }
    if (!output->model().isEmpty()) {
        return output->model();
    }
    if (!output->vendor().isEmpty()) {
        return output->vendor();
    }
    return QStringLiteral("Display");
}

QString KScreenOutputModel::formatResolution(const QSize &size)
{
    return QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
}

QString KScreenOutputModel::formatRefreshRate(float refreshRate)
{
    const int roundedRefreshRate = qRound(refreshRate);
    if (qAbs(refreshRate - roundedRefreshRate) < 0.01) {
        return QString::number(roundedRefreshRate) + QStringLiteral(" Hz");
    }
    return QString::number(refreshRate, 'f', 2) + QStringLiteral(" Hz");
}

bool KScreenOutputModel::sameRefreshRate(float first, float second)
{
    return qAbs(first - second) < 0.01;
}

QVariant KScreenOutputModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    const KScreen::OutputPtr output = outputAt(index.row());
    if (!output) {
        return QVariant();
    }

    const KScreen::ModePtr currentMode = output->currentMode();
    const QSize currentModeSize = currentMode ? currentMode->size() : output->enforcedModeSize();
    const QList<QSize> resolutions = resolutionsForOutput(output);
    const QList<float> refreshRates = refreshRatesForOutput(output, currentModeSize);

    switch (role) {
    case Qt::DisplayRole:
        return outputName(output);
    case EnabledRole:
        return output->isEnabled();
    case InternalRole:
        return output->type() == KScreen::Output::Panel
            || outputName(output).startsWith(QStringLiteral("eDP"), Qt::CaseInsensitive)
            || outputName(output).startsWith(QStringLiteral("LVDS"), Qt::CaseInsensitive);
    case PrimaryRole:
        return m_config && m_config->primaryOutput() == output;
    case SizeRole:
        return output->size();
    case PositionRole:
    case NormalizedPositionRole:
        return output->pos();
    case AutoRotateRole:
        return output->autoRotatePolicy() != KScreen::Output::AutoRotatePolicy::Never;
    case AutoRotateOnlyInTabletModeRole:
        return output->autoRotatePolicy() == KScreen::Output::AutoRotatePolicy::InTabletMode;
    case RotationRole:
        return static_cast<int>(output->rotation());
    case ScaleRole:
        return output->scale();
    case ResolutionIndexRole: {
        const int resolutionIndex = resolutions.indexOf(currentModeSize);
        return resolutionIndex >= 0 ? resolutionIndex : 0;
    }
    case ResolutionsRole: {
        QStringList result;
        for (const QSize &resolution : resolutions) {
            result.append(formatResolution(resolution));
        }
        return result;
    }
    case RefreshRateIndexRole: {
        if (!currentMode) {
            return 0;
        }
        for (int i = 0; i < refreshRates.size(); ++i) {
            if (sameRefreshRate(refreshRates.at(i), currentMode->refreshRate())) {
                return i;
            }
        }
        return 0;
    }
    case RefreshRatesRole: {
        QStringList result;
        for (const float refreshRate : refreshRates) {
            result.append(formatRefreshRate(refreshRate));
        }
        return result;
    }
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

bool KScreenOutputModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) {
        return false;
    }

    const KScreen::OutputPtr output = outputAt(index.row());
    if (!output) {
        return false;
    }

    bool changed = false;
    switch (role) {
    case EnabledRole: {
        const bool enabled = value.toBool();
        if (!enabled) {
            int enabledOutputs = 0;
            for (const KScreen::OutputPtr &candidate : m_outputs) {
                enabledOutputs += candidate && candidate->isEnabled() ? 1 : 0;
            }
            if (enabledOutputs <= 1) {
                return false;
            }
        }
        if (output->isEnabled() != enabled) {
            output->setEnabled(enabled);
            changed = true;
        }
        break;
    }
    case RotationRole: {
        const auto rotation = static_cast<KScreen::Output::Rotation>(value.toInt());
        if (rotation != KScreen::Output::None
            && rotation != KScreen::Output::Left
            && rotation != KScreen::Output::Inverted
            && rotation != KScreen::Output::Right) {
            return false;
        }
        if (output->rotation() != rotation) {
            output->setRotation(rotation);
            changed = true;
        }
        break;
    }
    case ResolutionIndexRole: {
        const QList<QSize> resolutions = resolutionsForOutput(output);
        const int resolutionIndex = value.toInt();
        if (resolutionIndex < 0 || resolutionIndex >= resolutions.size()) {
            return false;
        }

        const QSize selectedSize = resolutions.at(resolutionIndex);
        const KScreen::ModePtr currentMode = output->currentMode();
        const float currentRefreshRate = currentMode ? currentMode->refreshRate() : 0;
        QString selectedModeId;
        float closestRefreshRateDifference = std::numeric_limits<float>::max();
        for (const ModeChoice &mode : modesForOutput(output)) {
            if (mode.size != selectedSize) {
                continue;
            }
            const float difference = qAbs(mode.refreshRate - currentRefreshRate);
            if (selectedModeId.isEmpty() || difference < closestRefreshRateDifference) {
                selectedModeId = mode.id;
                closestRefreshRateDifference = difference;
            }
        }
        if (!selectedModeId.isEmpty() && selectedModeId != output->currentModeId()) {
            output->setCurrentModeId(selectedModeId);
            changed = true;
        }
        break;
    }
    case RefreshRateIndexRole: {
        const KScreen::ModePtr currentMode = output->currentMode();
        const QSize currentSize = currentMode ? currentMode->size() : output->enforcedModeSize();
        const QList<float> refreshRates = refreshRatesForOutput(output, currentSize);
        const int refreshRateIndex = value.toInt();
        if (refreshRateIndex < 0 || refreshRateIndex >= refreshRates.size()) {
            return false;
        }

        const float selectedRefreshRate = refreshRates.at(refreshRateIndex);
        QString selectedModeId;
        for (const ModeChoice &mode : modesForOutput(output)) {
            if (mode.size == currentSize && sameRefreshRate(mode.refreshRate, selectedRefreshRate)) {
                selectedModeId = mode.id;
                break;
            }
        }
        if (!selectedModeId.isEmpty() && selectedModeId != output->currentModeId()) {
            output->setCurrentModeId(selectedModeId);
            changed = true;
        }
        break;
    }
    case ScaleRole: {
        const qreal scale = value.toReal();
        if (scale <= 0 || qFuzzyCompare(output->scale(), scale)) {
            return false;
        }
        output->setScale(scale);
        changed = true;
        break;
    }
    default:
        return false;
    }

    if (changed) {
        emit dataChanged(index, index, {role});
    }
    return changed;
}

Qt::ItemFlags KScreenOutputModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> KScreenOutputModel::roleNames() const
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

void KScreenOutputModel::setConfig(const KScreen::ConfigPtr &config)
{
    beginResetModel();
    m_config = config;
    m_outputs.clear();
    if (m_config) {
        const KScreen::OutputList outputs = m_config->outputs();
        for (auto it = outputs.cbegin(); it != outputs.cend(); ++it) {
            if (it.value() && it.value()->isConnected()) {
                m_outputs.append(it.value());
            }
        }
    }
    endResetModel();
}
