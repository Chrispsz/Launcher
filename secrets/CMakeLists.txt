add_library(secrets STATIC Secrets.cpp Secrets.h)
target_link_libraries(secrets Qt5::Core)
target_compile_definitions(secrets PUBLIC -DEMBED_SECRETS)
target_include_directories(secrets PUBLIC .)

set(Launcher_CommonName "LauncherMC")

set(Launcher_Copyright "Chrispsz" PARENT_SCOPE)
set(Launcher_Domain "" PARENT_SCOPE)
set(Launcher_Name "${Launcher_CommonName}" PARENT_SCOPE)
set(Launcher_DisplayName "${Launcher_CommonName}" PARENT_SCOPE)
set(Launcher_UserAgent "LauncherMC/1.0" PARENT_SCOPE)
set(Launcher_ConfigFile "launchermc.cfg" PARENT_SCOPE)
set(Launcher_Git "https://github.com/Chrispsz/Launcher" PARENT_SCOPE)

set(Launcher_Branding_ICNS "secrets/Launcher.icns" PARENT_SCOPE)
set(Launcher_Branding_WindowsRC "secrets/launcher.rc" PARENT_SCOPE)
set(Launcher_Branding_LogoQRC "secrets/logo.qrc" PARENT_SCOPE)
