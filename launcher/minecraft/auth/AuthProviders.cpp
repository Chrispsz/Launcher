#include "AuthProviders.h"
#include "providers/LocalAuthProvider.h"
#include "../../AuthServer.h"

#define REGISTER_AUTH_PROVIDER(Provider)          \
  {                                               \
    AuthProviderPtr provider(new Provider());     \
    m_providers.insert(provider->id(), provider); \
    provider->setAuthServer(authserver);          \
  }

namespace AuthProviders
{
    QMap<QString, AuthProviderPtr> m_providers;

    void load(std::shared_ptr<AuthServer> authserver)
    {
        REGISTER_AUTH_PROVIDER(LocalAuthProvider);
    }

    AuthProviderPtr lookup(QString id)
    {
        qDebug() << "LOOKUP AUTH_PROVIDER" << id;
        if (m_providers.contains(id))
            return m_providers.value(id);

        qDebug() << "Lookup failed";
        return nullptr;
    }

    QList<AuthProviderPtr> getAll() {
        return m_providers.values();
    }
}
