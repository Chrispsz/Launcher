#include "AccountData.h"
#include "AuthProviders.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUuid>

namespace {
void tokenToJSONV3(QJsonObject &parent, Katabasis::Token t, const char * tokenName) {
    if(!t.persistent) {
        return;
    }
    QJsonObject out;
    if(t.issueInstant.isValid()) {
        out["iat"] = QJsonValue(t.issueInstant.toMSecsSinceEpoch() / 1000);
    }

    if(t.notAfter.isValid()) {
        out["exp"] = QJsonValue(t.notAfter.toMSecsSinceEpoch() / 1000);
    }

    bool save = false;
    if(!t.token.isEmpty()) {
        out["token"] = QJsonValue(t.token);
        save = true;
    }
    if(!t.refresh_token.isEmpty()) {
        out["refresh_token"] = QJsonValue(t.refresh_token);
        save = true;
    }
    if(t.extra.size()) {
        out["extra"] = QJsonObject::fromVariantMap(t.extra);
        save = true;
    }
    if(save) {
        parent[tokenName] = out;
    }
}

Katabasis::Token tokenFromJSONV3(const QJsonObject &parent, const char * tokenName) {
    Katabasis::Token out;
    auto tokenObject = parent.value(tokenName).toObject();
    if(tokenObject.isEmpty()) {
        return out;
    }
    auto issueInstant = tokenObject.value("iat");
    if(issueInstant.isDouble()) {
        out.issueInstant = QDateTime::fromMSecsSinceEpoch(((int64_t) issueInstant.toDouble()) * 1000);
    }

    auto notAfter = tokenObject.value("exp");
    if(notAfter.isDouble()) {
        out.notAfter = QDateTime::fromMSecsSinceEpoch(((int64_t) notAfter.toDouble()) * 1000);
    }

    auto token = tokenObject.value("token");
    if(token.isString()) {
        out.token = token.toString();
        out.validity = Katabasis::Validity::Assumed;
    }

    auto refresh_token = tokenObject.value("refresh_token");
    if(refresh_token.isString()) {
        out.refresh_token = refresh_token.toString();
    }

    auto extra = tokenObject.value("extra");
    if(extra.isObject()) {
        out.extra = extra.toObject().toVariantMap();
    }
    return out;
}

void profileToJSONV3(QJsonObject &parent, MinecraftProfile p, const char * tokenName) {
    if(p.id.isEmpty()) {
        return;
    }
    QJsonObject out;
    out["id"] = QJsonValue(p.id);
    out["name"] = QJsonValue(p.name);
    if(!p.currentCape.isEmpty()) {
        out["cape"] = p.currentCape;
    }

    {
        QJsonObject skinObj;
        skinObj["id"] = p.skin.id;
        skinObj["url"] = p.skin.url;
        skinObj["variant"] = p.skin.variant;
        if(p.skin.data.size()) {
            skinObj["data"] = QString::fromLatin1(p.skin.data.toBase64());
        }
        out["skin"] = skinObj;
    }

    QJsonArray capesArray;
    for(auto & cape: p.capes) {
        QJsonObject capeObj;
        capeObj["id"] = cape.id;
        capeObj["url"] = cape.url;
        capeObj["alias"] = cape.alias;
        if(cape.data.size()) {
            capeObj["data"] = QString::fromLatin1(cape.data.toBase64());
        }
        capesArray.push_back(capeObj);
    }
    out["capes"] = capesArray;
    parent[tokenName] = out;
}

MinecraftProfile profileFromJSONV3(const QJsonObject &parent, const char * tokenName) {
    MinecraftProfile out;
    auto tokenObject = parent.value(tokenName).toObject();
    if(tokenObject.isEmpty()) {
        return out;
    }
    {
        auto idV = tokenObject.value("id");
        auto nameV = tokenObject.value("name");
        if(!idV.isString() || !nameV.isString()) {
            qWarning() << "mandatory profile attributes are missing or of unexpected type";
            return MinecraftProfile();
        }
        out.name = nameV.toString();
        out.id = idV.toString();
    }

    {
        auto skinV = tokenObject.value("skin");
        if(!skinV.isObject()) {
            qWarning() << "skin is missing";
            return MinecraftProfile();
        }
        auto skinObj = skinV.toObject();
        auto idV = skinObj.value("id");
        auto urlV = skinObj.value("url");
        auto variantV = skinObj.value("variant");
        if(!idV.isString() || !urlV.isString() || !variantV.isString()) {
            qWarning() << "mandatory skin attributes are missing or of unexpected type";
            return MinecraftProfile();
        }
        out.skin.id = idV.toString();
        out.skin.url = urlV.toString();
        out.skin.variant = variantV.toString();

        // data for skin is optional
        auto dataV = skinObj.value("data");
        if(dataV.isString()) {
            // TODO: validate base64
            out.skin.data = QByteArray::fromBase64(dataV.toString().toLatin1());
        }
        else if (!dataV.isUndefined()) {
            qWarning() << "skin data is something unexpected";
            return MinecraftProfile();
        }
    }

    {
        auto capesV = tokenObject.value("capes");
        if(!capesV.isArray()) {
            qWarning() << "capes is not an array!";
            return MinecraftProfile();
        }
        auto capesArray = capesV.toArray();
        for(auto capeV: capesArray) {
            if(!capeV.isObject()) {
                qWarning() << "cape is not an object!";
                return MinecraftProfile();
            }
            auto capeObj = capeV.toObject();
            auto idV = capeObj.value("id");
            auto urlV = capeObj.value("url");
            auto aliasV = capeObj.value("alias");
            if(!idV.isString() || !urlV.isString() || !aliasV.isString()) {
                qWarning() << "mandatory skin attributes are missing or of unexpected type";
                return MinecraftProfile();
            }
            Cape cape;
            cape.id = idV.toString();
            cape.url = urlV.toString();
            cape.alias = aliasV.toString();

            // data for cape is optional.
            auto dataV = capeObj.value("data");
            if(dataV.isString()) {
                // TODO: validate base64
                cape.data = QByteArray::fromBase64(dataV.toString().toLatin1());
            }
            else if (!dataV.isUndefined()) {
                qWarning() << "cape data is something unexpected";
                return MinecraftProfile();
            }
            out.capes[cape.id] = cape;
        }
    }
    // current cape
    {
        auto capeV = tokenObject.value("cape");
        if(capeV.isString()) {
            auto currentCape = capeV.toString();
            if(out.capes.contains(currentCape)) {
                out.currentCape = currentCape;
            }
        }
    }
    out.validity = Katabasis::Validity::Assumed;
    return out;
}

void entitlementToJSONV3(QJsonObject &parent, MinecraftEntitlement p) {
    if(p.validity == Katabasis::Validity::None) {
        return;
    }
    QJsonObject out;
    out["ownsMinecraft"] = QJsonValue(p.ownsMinecraft);
    out["canPlayMinecraft"] = QJsonValue(p.canPlayMinecraft);
    parent["entitlement"] = out;
}

bool entitlementFromJSONV3(const QJsonObject &parent, MinecraftEntitlement & out) {
    auto entitlementObject = parent.value("entitlement").toObject();
    if(entitlementObject.isEmpty()) {
        return false;
    }
    {
        auto ownsMinecraftV = entitlementObject.value("ownsMinecraft");
        auto canPlayMinecraftV = entitlementObject.value("canPlayMinecraft");
        if(!ownsMinecraftV.isBool() || !canPlayMinecraftV.isBool()) {
            qWarning() << "mandatory attributes are missing or of unexpected type";
            return false;
        }
        out.canPlayMinecraft = canPlayMinecraftV.toBool(false);
        out.ownsMinecraft = ownsMinecraftV.toBool(false);
        out.validity = Katabasis::Validity::Assumed;
    }
    return true;
}

}

bool AccountData::resumeStateFromV2(QJsonObject data) {
    // V2 format: Mojang-only. Migrate to Local.
    if (!data.value("username").isString())
    {
        qCritical() << "Não foi possível carregar conta do formato antigo. Campo username ausente.";
        return false;
    }

    QString userName = data.value("username").toString("");
    QString clientToken = data.value("clientToken").toString("");
    QString accessToken = data.value("accessToken").toString("");

    QJsonArray profileArray = data.value("profiles").toArray();
    if (profileArray.size() < 1)
    {
        qCritical() << "Não foi possível carregar conta com username \"" << userName << "\". Nenhum perfil encontrado.";
        return false;
    }

    struct AccountProfile
    {
        QString id;
        QString name;
        bool legacy;
    };

    QList<AccountProfile> profiles;
    int currentProfileIndex = 0;
    int index = -1;
    QString currentProfile = data.value("activeProfile").toString("");
    for (QJsonValue profileVal : profileArray)
    {
        index++;
        QJsonObject profileObject = profileVal.toObject();
        QString id = profileObject.value("id").toString("");
        QString name = profileObject.value("name").toString("");
        bool legacy = profileObject.value("legacy").toBool(false);
        if (id.isEmpty() || name.isEmpty())
        {
            qWarning() << "Não foi possível carregar o perfil" << name << "porque estava sem ID ou nome.";
            continue;
        }
        if(id == currentProfile) {
            currentProfileIndex = index;
        }
        profiles.append({id, name, legacy});
    }
    auto & profile = profiles[currentProfileIndex];

    // Migrate old Mojang accounts to Local
    type = AccountType::Local;
    legacy = profile.legacy;
    provider = AuthProviders::lookup("local");

    minecraftProfile.id = profile.id;
    minecraftProfile.name = profile.name;
    minecraftProfile.validity = Katabasis::Validity::Assumed;

    yggdrasilToken.token = accessToken;
    yggdrasilToken.extra["clientToken"] = clientToken;
    yggdrasilToken.extra["userName"] = userName;
    yggdrasilToken.validity = Katabasis::Validity::Assumed;

    validity_ = minecraftProfile.validity;
    return true;
}

bool AccountData::resumeStateFromV3(QJsonObject data) {
    auto typeV = data.value("type");
    if(!typeV.isString()) {
        qWarning() << "Falha ao analisar dados da conta: tipo ausente.";
        return false;
    }
    auto typeS = typeV.toString();

    // All account types are now Local — migrate from any old type
    type = AccountType::Local;
    provider = AuthProviders::lookup("local");

    if (!provider) {
        qWarning() << "Provedor local não encontrado!";
        return false;
    }

    // Old MSA/Elyby/Mojang fields are ignored; only yggdrasil + profile data is loaded
    yggdrasilToken = tokenFromJSONV3(data, "ygg");
    minecraftProfile = profileFromJSONV3(data, "profile");
    if(!entitlementFromJSONV3(data, minecraftEntitlement)) {
        if(minecraftProfile.validity != Katabasis::Validity::None) {
            minecraftEntitlement.canPlayMinecraft = true;
            minecraftEntitlement.ownsMinecraft = true;
            minecraftEntitlement.validity = Katabasis::Validity::Assumed;
        }
    }

    validity_ = minecraftProfile.validity;
    return true;
}

QJsonObject AccountData::saveState() const {
    QJsonObject output;
    output["type"] = "Local";
    tokenToJSONV3(output, yggdrasilToken, "ygg");
    profileToJSONV3(output, minecraftProfile, "profile");
    entitlementToJSONV3(output, minecraftEntitlement);
    return output;
}

QString AccountData::userName() const {
    return yggdrasilToken.extra["userName"].toString();
}

QString AccountData::accessToken() const {
    return yggdrasilToken.token;
}

QString AccountData::clientToken() const {
    return yggdrasilToken.extra["clientToken"].toString();
}

void AccountData::setClientToken(QString clientToken) {
    yggdrasilToken.extra["clientToken"] = clientToken;
}

void AccountData::generateClientTokenIfMissing() {
    if(yggdrasilToken.extra.contains("clientToken")) {
        return;
    }
    invalidateClientToken();
}

void AccountData::invalidateClientToken() {
    yggdrasilToken.extra["clientToken"] = QUuid::createUuid().toString().remove(QRegExp("[{-}]"));
}

QString AccountData::profileId() const {
    return minecraftProfile.id;
}

QString AccountData::profileName() const {
    if(minecraftProfile.name.size() == 0) {
        return QObject::tr("Sem perfil (%1)").arg(accountDisplayString());
    }
    else {
        return minecraftProfile.name;
    }
}

QString AccountData::accountDisplayString() const {
    return QObject::tr("<Local>");
}

QString AccountData::lastError() const {
    return errorString;
}
