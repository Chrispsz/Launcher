#pragma once
#include <QString>
#include <QByteArray>
#include <QVector>
#include <katabasis/Bits.h>
#include <QJsonObject>

#include "providers/BaseAuthProvider.h"

struct Skin {
    QString id;
    QString url;
    QString variant;

    QByteArray data;
};

struct Cape {
    QString id;
    QString url;
    QString alias;

    QByteArray data;
};

struct MinecraftEntitlement {
    bool ownsMinecraft = false;
    bool canPlayMinecraft = false;
    Katabasis::Validity validity = Katabasis::Validity::None;
};

struct MinecraftProfile {
    QString id;
    QString name;
    Skin skin;
    QString currentCape;
    QMap<QString, Cape> capes;
    Katabasis::Validity validity = Katabasis::Validity::None;
};

enum class AccountType {
    Local
};

enum class AccountState {
    Unchecked,
    Offline,
    Working,
    Online,
    Errored,
    Expired,
    Gone
};

struct AccountData {
    QJsonObject saveState() const;
    bool resumeStateFromV2(QJsonObject data);
    bool resumeStateFromV3(QJsonObject data);

    AuthProviderPtr provider;

    QString accountDisplayString() const;

    QString userName() const;

    QString clientToken() const;
    void setClientToken(QString clientToken);
    void invalidateClientToken();
    void generateClientTokenIfMissing();

    //! Yggdrasil access token, as passed to the game.
    QString accessToken() const;

    QString profileId() const;
    QString profileName() const;

    QString lastError() const;

    AccountType type = AccountType::Local;
    bool legacy = false;

    Katabasis::Token yggdrasilToken;
    MinecraftProfile minecraftProfile;
    MinecraftEntitlement minecraftEntitlement;
    Katabasis::Validity validity_ = Katabasis::Validity::None;

    // runtime only information (not saved with the account)
    QString internalId;
    QString errorString;
    AccountState accountState = AccountState::Unchecked;
};
