#pragma once

#include <QObject>
#include "QObjectPtr.h"
#include <QDateTime>
#include <QSet>
#include <QProcess>
#include <QUrl>
#include "../../../AuthServer.h"

class BaseAuthProvider;
typedef std::shared_ptr<BaseAuthProvider> AuthProviderPtr;

/*!
 * \brief Base class for auth provider.
 * Only LocalAuthProvider exists now. This interface is kept for
 * extensibility in case future providers are added.
 */
class BaseAuthProvider : public QObject
{
    Q_OBJECT

public:
    virtual ~BaseAuthProvider(){};

    // Unique id for provider
    virtual QString id()
    {
        return "base";
    };

    // Name of provider that displayed in account selector and list
    virtual QString displayName()
    {
        return "Base";
    };

    // Endpoint for authlib injector (use empty if authlib injector isn't required)
    virtual QString injectorEndpoint()
    {
        return "";
    };

    bool setAuthServer(std::shared_ptr<AuthServer> authServer)
    {
        m_authServer = authServer;
        return true;
    }

protected:
    std::shared_ptr<AuthServer> m_authServer;
};
