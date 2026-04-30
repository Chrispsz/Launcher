#pragma once

#include "BaseAuthProvider.h"

class LocalAuthProvider : public BaseAuthProvider
{
    Q_OBJECT

public:
    QString id() override
    {
        return "local";
    }

    QString displayName() override
    {
        return "Local";
    }

    QString injectorEndpoint() override
    {
        return ((QString)"http://127.0.0.1:%1").arg(m_authServer->port());
    };
};
