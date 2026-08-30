#pragma once

#include <QObject>

class Qt5OutputModel;
class QScreen;

class Qt5Screen : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Qt5OutputModel *outputModel READ outputModel NOTIFY outputModelChanged)

public:
    explicit Qt5Screen(QObject *parent = nullptr);

    Qt5OutputModel *outputModel() const;
    Q_INVOKABLE void save();

private slots:
    void refreshScreens();
    void connectScreen(QScreen *screen);

signals:
    void outputModelChanged();

private:
    Qt5OutputModel *m_outputModel;
};

