#pragma once

#include <QString>
#include <QMultiMap>
#include <memory>
#include "QObjectPtr.h"

class MinecraftAccount;
class QNetworkAccessManager;

struct AuthSession
{
    bool MakeOffline(QString offline_playername);
    void MakeDemo();

    QString serializeUserProperties();

    enum Status
    {
        Undetermined,
        RequiresProfileSetup,
        PlayableOffline,
        PlayableOnline
    } status = Undetermined;

    // client token
    QString client_token;
    // account user name
    QString username;
    // combined session ID
    QString session;
    // volatile auth token
    QString access_token;
    // profile name
    QString player_name;
    // profile ID
    QString uuid;
    // 'legacy' or 'mojang', depending on account type
    QString user_type;
    // Did the user request online mode?
    bool wants_online = true;

    //Is this a demo session?
    bool demo = false;
};

typedef std::shared_ptr<AuthSession> AuthSessionPtr;
