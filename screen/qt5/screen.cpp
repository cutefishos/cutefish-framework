#include "screen.h"

#include "outputmodel.h"

#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/SetConfigOperation>

#include <QDebug>
#include <QGuiApplication>
#include <QTimer>

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
    KScreen::SetConfigOperation operation(m_config, this);
    const bool applied = operation.exec() && !operation.hasError();
    if (!applied) {
        qWarning() << "Unable to apply the display configuration through KWin Wayland:"
                   << operation.errorString();
    }
    m_applying = false;

    loadConfig();
}
