#pragma once

#include <KScreen/Config>

#include <QObject>

class KScreenOutputModel;

class KScreenScreen : public QObject
{
    Q_OBJECT
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
