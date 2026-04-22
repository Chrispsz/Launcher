#pragma once
#include <QString>

class Config
{
public:
    Config();
    QString LAUNCHER_NAME;
    QString LAUNCHER_DISPLAYNAME;
    QString LAUNCHER_COPYRIGHT;
    QString LAUNCHER_DOMAIN;
    QString LAUNCHER_CONFIGFILE;
    QString LAUNCHER_GIT;

    int VERSION_MAJOR;
    int VERSION_MINOR;
    int VERSION_HOTFIX;
    int VERSION_BUILD;

    QString VERSION_CHANNEL;
    QString BUILD_PLATFORM;
    QString UPDATER_BASE;
    QString USER_AGENT;
    QString USER_AGENT_UNCACHED;

    QString FULL_VERSION_STR;
    QString GIT_COMMIT;
    QString GIT_REFSPEC;
    QString VERSION_STR;

    QString META_URL;

    QString RESOURCE_BASE = "https://resources.download.minecraft.net/";
    QString LIBRARY_BASE = "https://libraries.minecraft.net/";
    QString AUTH_BASE = "https://authserver.mojang.com/";

    QString printableVersionString() const;
};

extern const Config BuildConfig;
