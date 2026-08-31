#include "screen.h"

#include "outputmodel.h"

#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/SetConfigOperation>

#include <QDebug>
#include <QGuiApplication>
#include <QSettings>
#include <QTimer>

static bool isDynamicDisplay(const KScreen::OutputPtr &output)
{
    if (!output) {
        return false;
    }

    const QString identity = QStringLiteral("%1 %2 %3")
                                 .arg(output->name(), output->vendor(), output->model());
    return identity.contains(QStringLiteral("virtual"), Qt::CaseInsensitive)
        || identity.contains(QStringLiteral("qemu"), Qt::CaseInsensitive)
        || identity.contains(QStringLiteral("spice"), Qt::CaseInsensitive)
        || identity.contains(QStringLiteral("virtio"), Qt::CaseInsensitive);
}

static QString displayOutputKey(const KScreen::OutputPtr &output)
{
    if (!output) {
        return QString();
    }

    // Virtual outputs can change their EDID/mode list whenever the host
    // resizes the guest window. Their hash is therefore not a stable
    // identity; use the compositor's output name instead.
    QString key = isDynamicDisplay(output) ? output->name() : output->hashMd5();
    if (key.isEmpty()) {
        key = output->name();
    }
    if (key.isEmpty()) {
        key = QString::number(output->id());
    }
    return key.replace(QLatin1Char('/'), QLatin1Char('_'));
}

static void saveDisplayConfiguration(const KScreen::ConfigPtr &config)
{
    if (!config) {
        return;
    }

    QSettings settings(QSettings::UserScope, QStringLiteral("cutefishos"), QStringLiteral("display"));
    settings.setValue(QStringLiteral("Version"), 1);
    settings.beginGroup(QStringLiteral("Outputs"));
    const KScreen::OutputList outputs = config->outputs();
    for (auto it = outputs.cbegin(); it != outputs.cend(); ++it) {
        const KScreen::OutputPtr output = it.value();
        if (!output || !output->isConnected()) {
            continue;
        }

        settings.beginGroup(displayOutputKey(output));
        settings.setValue(QStringLiteral("Name"), output->name());
        settings.setValue(QStringLiteral("Enabled"), output->isEnabled());
        settings.setValue(QStringLiteral("Priority"), output->priority());
        settings.setValue(QStringLiteral("PositionX"), output->pos().x());
        settings.setValue(QStringLiteral("PositionY"), output->pos().y());
        settings.setValue(QStringLiteral("Scale"), output->scale());
        settings.setValue(QStringLiteral("Rotation"), static_cast<int>(output->rotation()));

        // Virtual SPICE/QEMU modes follow the host window. Persisting the
        // current mode would make the next Wayland session restore an old
        // UTM size instead of accepting the host's current size.
        if (isDynamicDisplay(output)) {
            settings.remove(QStringLiteral("ModeId"));
            settings.remove(QStringLiteral("ModeWidth"));
            settings.remove(QStringLiteral("ModeHeight"));
            settings.remove(QStringLiteral("ModeRefreshRate"));
        } else if (const KScreen::ModePtr mode = output->currentMode()) {
            settings.setValue(QStringLiteral("ModeId"), mode->id());
            settings.setValue(QStringLiteral("ModeWidth"), mode->size().width());
            settings.setValue(QStringLiteral("ModeHeight"), mode->size().height());
            settings.setValue(QStringLiteral("ModeRefreshRate"), mode->refreshRate());
        }
        settings.endGroup();
    }
    settings.endGroup();
    settings.sync();

    KScreen::OutputPtr primary = config->primaryOutput();
    if (!primary) {
        for (const KScreen::OutputPtr &output : config->connectedOutputs()) {
            if (output && output->isEnabled()) {
                primary = output;
                break;
            }
        }
    }
    if (primary) {
        QSettings themeSettings(QSettings::UserScope,
                                QStringLiteral("cutefishos"),
                                QStringLiteral("theme"));
        themeSettings.setValue(QStringLiteral("PixelRatio"), primary->scale());
        themeSettings.sync();
    }
}

KScreenScreen::KScreenScreen(QObject *parent)
    : QObject(parent)
    , m_outputModel(new KScreenOutputModel(this))
{
    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)
        && qEnvironmentVariableIsEmpty("KSCREEN_BACKEND")) {
        qputenv("KSCREEN_BACKEND", QByteArrayLiteral("kwayland"));
    }

    connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged,
            this, [this] {
                if (!m_applying) {
                    loadConfig();
                }
            });

    QTimer::singleShot(0, this, &KScreenScreen::loadConfig);
}

KScreenOutputModel *KScreenScreen::outputModel() const
{
    return m_outputModel;
}

void KScreenScreen::loadConfig()
{
    if (m_applying) {
        return;
    }

    KScreen::GetConfigOperation operation(KScreen::ConfigOperation::NoOptions, this);
    if (!operation.exec() || operation.hasError() || !operation.config()) {
        qWarning() << "Unable to read the display configuration from KScreen:"
                   << operation.errorString();
        if (m_config) {
            KScreen::ConfigMonitor::instance()->removeConfig(m_config);
        }
        m_config.clear();
        m_outputModel->setConfig({});
        emit outputModelChanged();
        return;
    }

    if (m_config) {
        KScreen::ConfigMonitor::instance()->removeConfig(m_config);
    }
    m_config = operation.config();
    KScreen::ConfigMonitor::instance()->addConfig(m_config);
    m_outputModel->setConfig(m_config);
    emit outputModelChanged();
}

void KScreenScreen::save()
{
    if (!m_config || m_applying) {
        return;
    }

    if (!KScreen::Config::canBeApplied(m_config,
                                       KScreen::Config::ValidityFlag::RequireAtLeastOneEnabledScreen)) {
        qWarning() << "The requested display configuration is not valid";
        loadConfig();
        return;
    }

    m_applying = true;
    // Cutefish owns the persistent display configuration. KWin is only the
    // live display server to which this snapshot is applied.
    saveDisplayConfiguration(m_config);

    KScreen::SetConfigOperation operation(m_config, this);
    const bool applied = operation.exec() && !operation.hasError();
    if (!applied) {
        qWarning() << "Unable to apply the display configuration through KWin Wayland:"
                   << operation.errorString();
    }
    m_applying = false;

    loadConfig();
}
