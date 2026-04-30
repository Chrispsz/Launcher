#pragma once

#include <QString>
#include <QTcpServer>
#include <QMutex>
#include "settings/SettingsObject.h"

class AuthServer: public QObject
{
public:
    explicit AuthServer(QObject *parent = 0);

    quint16 port();

    /// Set the current profile info for the game session.
    /// Called before launching the game so profile lookups return the correct player.
    void setProfileInfo(const QString &profileId, const QString &profileName);

    QString profileId() const { return m_profileId; }
    QString profileName() const { return m_profileName; }

private:
    void newConnection();

private:
    std::shared_ptr<QTcpServer> m_tcpServer;
    QMutex m_profileMutex;
    QString m_profileId;
    QString m_profileName;
};
