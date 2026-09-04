#pragma once

#include <KScreen/Config>

#include <QObject>
#include <QtQml/qqmlregistration.h>

class KScreenOutputModel;

class KScreenScreen : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Screen)
    Q_PROPERTY(KScreenOutputModel *outputModel READ outputModel NOTIFY outputModelChanged)

public:
    explicit KScreenScreen(QObject *parent = nullptr);

    KScreenOutputModel *outputModel() const;
    Q_INVOKABLE void save();

signals:
    void outputModelChanged();

private slots:
    void loadConfig();

private:
    KScreen::ConfigPtr m_config;
    KScreenOutputModel *m_outputModel;
    bool m_applying = false;
};
