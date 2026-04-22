#include "LaunchController.h"
#include "minecraft/auth/AccountList.h"
#include "Application.h"

#include "ui/MainWindow.h"
#include "ui/InstanceWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"


#include <QLineEdit>
#include <QInputDialog>
#include <QStringList>
#include <QHostInfo>
#include <QList>
#include <QHostAddress>
#include <QPushButton>

#include "BuildConfig.h"
#include "JavaCommon.h"
#include "tasks/Task.h"
#include "minecraft/auth/AccountTask.h"
#include "launch/steps/TextPrint.h"

LaunchController::LaunchController(QObject *parent) : Task(parent)
{
}

void LaunchController::executeTask()
{
    if (!m_instance)
    {
        emitFailed(tr("No instance specified!"));
        return;
    }

    if(!JavaCommon::checkJVMArgs(m_instance->settings()->get("JvmArgs").toString(), m_parentWidget)) {
        emitFailed(tr("Invalid Java arguments specified. Please fix this first."));
        return;
    }

    login();
}

void LaunchController::decideAccount()
{
    if(m_accountToUse) {
        return;
    }

    // Find an account to use.
    auto accounts = APPLICATION->accounts();
    if (accounts->count() <= 0)
    {
        // Tell the user they need to log in at least one account in order to play.
        auto reply = CustomMessageBox::selectable(
            m_parentWidget,
            tr("No Accounts"),
            tr("In order to play Minecraft, you must have at least one Mojang or Minecraft "
               "account logged in."
               "Would you like to open the account manager to add an account now?"),
            QMessageBox::Information,
            QMessageBox::Yes | QMessageBox::No
        )->exec();

        if (reply == QMessageBox::Yes)
        {
            // Open the account manager.
            APPLICATION->ShowGlobalSettings(m_parentWidget, "accounts");
        }
    }

    m_accountToUse = accounts->defaultAccount();
    if (!m_accountToUse)
    {
        // No default account set - just use the first available one
        m_accountToUse = accounts->defaultAccount();
    }
}


void LaunchController::login() {
    decideAccount();

    // if no account is selected, we bail
    if (!m_accountToUse)
    {
        emitFailed(tr("No account selected for launch."));
        return;
    }

    // we try empty password first :)
    QString password;
    // we loop until the user succeeds in logging in or gives up
    bool tryagain = true;
    // the failure. the default failure.
    const QString needLoginAgain = tr("Your account is currently not logged in. Please enter your password to log in again. <br /> <br /> This could be caused by a password change.");
    QString failReason = needLoginAgain;

    while (tryagain)
    {
        m_session = std::make_shared<AuthSession>();
        m_session->wants_online = m_online;
        m_accountToUse->fillSession(m_session);

        if (m_accountToUse->typeString() == "local" || m_accountToUse->typeString() == "elyby") {
            launchInstance();
            return;
        }

        switch(m_accountToUse->accountState()) {
            case AccountState::Offline: {
                m_session->wants_online = false;
                // NOTE: fallthrough is intentional
            }
            case AccountState::Online: {
                if(!m_session->wants_online) {
                    QString usedname;
                    if(m_offlineName.isEmpty()) {
                        // we ask the user for a player name
                        bool ok = false;
                        QString lastOfflinePlayerName = APPLICATION->settings()->get("LastOfflinePlayerName").toString();
                        usedname = lastOfflinePlayerName.isEmpty() ? m_session->player_name : lastOfflinePlayerName;
                        QString name = QInputDialog::getText(
                            m_parentWidget,
                            tr("Player name"),
                            tr("Choose your offline mode player name."),
                            QLineEdit::Normal,
                            usedname,
                            &ok
                        );
                        if (!ok)
                        {
                            tryagain = false;
                            break;
                        }
                        if (name.length())
                        {
                            usedname = name;
                            APPLICATION->settings()->set("LastOfflinePlayerName", usedname);
                        }
                    }
                    else {
                        usedname = m_offlineName;
                    }

                    m_session->MakeOffline(usedname);
                    // offline flavored game from here :3
                }
                if(m_accountToUse->ownsMinecraft()) {
                    if(!m_accountToUse->hasProfile()) {
                        // No profile - just launch offline
                        m_session->wants_online = false;
                        m_session->MakeOffline(m_accountToUse->profileId());
                        launchInstance();
                        return;
                    }
                    // we own Minecraft, there is a profile, it's all ready to go!
                    launchInstance();
                    return;
                }
                else {
                    // play demo ?
                    QMessageBox box(m_parentWidget);
                    box.setWindowTitle(tr("Play demo?"));
                    box.setText(tr("This account does not own Minecraft.\nYou need to purchase the game first to play it.\n\nDo you want to play the demo?"));
                    box.setIcon(QMessageBox::Warning);
                    auto demoButton = box.addButton(tr("Play Demo"), QMessageBox::ButtonRole::YesRole);
                    auto cancelButton = box.addButton(tr("Cancel"), QMessageBox::ButtonRole::NoRole);
                    box.setDefaultButton(cancelButton);

                    box.exec();
                    if(box.clickedButton() == demoButton) {
                        // play demo here
                        m_session->MakeDemo();
                        launchInstance();
                    }
                    else {
                        emitFailed(tr("Launch cancelled - account does not own Minecraft."));
                    }
                }
                return;
            }
            case AccountState::Errored:
                // This means some sort of soft error that we can fix with a refresh ... so let's refresh.
            case AccountState::Unchecked: {
                m_accountToUse->refresh();
                // NOTE: fallthrough intentional
            }
            case AccountState::Working: {
                // refresh is in progress, we need to wait for it to finish to proceed.
                ProgressDialog progDialog(m_parentWidget);
                if (m_online)
                {
                    progDialog.setSkipButton(true, tr("Play Offline"));
                }
                auto task = m_accountToUse->currentTask();
                progDialog.execWithTask(task.get());
                continue;
            }
            // FIXME: this is missing - the meaning is that the account is queued for refresh and we should wait for that
            /*
            case AccountState::Queued: {
                return;
            }
            */
            case AccountState::Expired: {
                // Account expired - just launch offline
                m_session->wants_online = false;
                m_session->MakeOffline(m_accountToUse->profileId());
                launchInstance();
                return;
            }
            case AccountState::Gone: {
                auto errorString = tr("The account no longer exists on the servers. It may have been migrated, in which case please add the new account you migrated this one to.");
                QMessageBox::warning(
                    m_parentWidget,
                    tr("Account gone"),
                    errorString,
                    QMessageBox::StandardButton::Ok,
                    QMessageBox::StandardButton::Ok
                );
                emitFailed(errorString);
                return;
            }
            case AccountState::MustMigrate: {
                auto errorString = tr("The account must be migrated to a Microsoft account.");
                QMessageBox::warning(
                    m_parentWidget,
                    tr("Account requires migration"),
                    errorString,
                    QMessageBox::StandardButton::Ok,
                    QMessageBox::StandardButton::Ok
                );
                emitFailed(errorString);
                return;
            }
        }
    }
    emitFailed(tr("Failed to launch."));
}

void LaunchController::launchInstance()
{
    Q_ASSERT_X(m_instance != NULL, "launchInstance", "instance is NULL");
    Q_ASSERT_X(m_session.get() != nullptr, "launchInstance", "session is NULL");

    if(!m_instance->reloadSettings())
    {
        QMessageBox::critical(m_parentWidget, tr("Error!"), tr("Couldn't load the instance profile."));
        emitFailed(tr("Couldn't load the instance profile."));
        return;
    }

    m_launcher = m_instance->createLaunchTask(m_session, m_quickPlayTarget, m_authserver->port());
    if (!m_launcher)
    {
        emitFailed(tr("Couldn't instantiate a launcher."));
        return;
    }

    auto console = qobject_cast<InstanceWindow *>(m_parentWidget);
    auto showConsole = m_instance->settings()->get("ShowConsole").toBool();
    if(!console && showConsole)
    {
        APPLICATION->showInstanceWindow(m_instance);
    }
    connect(m_launcher.get(), &LaunchTask::readyForLaunch, this, &LaunchController::readyForLaunch);
    connect(m_launcher.get(), &LaunchTask::succeeded, this, &LaunchController::onSucceeded);
    connect(m_launcher.get(), &LaunchTask::failed, this,  &LaunchController::onFailed);
    connect(m_launcher.get(), &LaunchTask::requestProgress, this, &LaunchController::onProgressRequested);

    // Prepend Online and Auth Status
    QString online_mode;
    if(m_session->wants_online) {
        online_mode = "online";

        // Prepend Server Status
        QStringList servers = {"authserver.mojang.com", "session.minecraft.net", "textures.minecraft.net", "api.mojang.com"};
        QString resolved_servers = "";
        QHostInfo host_info;

        for(QString server : servers) {
            host_info = QHostInfo::fromName(server);
            resolved_servers = resolved_servers + server + " resolves to:\n    [";
            if(!host_info.addresses().isEmpty()) {
                for(QHostAddress address : host_info.addresses()) {
                    resolved_servers = resolved_servers + address.toString();
                    if(!host_info.addresses().endsWith(address)) {
                        resolved_servers = resolved_servers + ", ";
                    }
                }
            } else {
                resolved_servers = resolved_servers + "N/A";
            }
            resolved_servers = resolved_servers + "]\n\n";
        }
        m_launcher->prependStep(new TextPrint(m_launcher.get(), resolved_servers, MessageLevel::Launcher));
    } else {
        online_mode = "offline";
    }

    m_launcher->prependStep(new TextPrint(m_launcher.get(), "Launched instance in " + online_mode + " mode\n", MessageLevel::Launcher));

    // Prepend Version
    m_launcher->prependStep(new TextPrint(m_launcher.get(), BuildConfig.LAUNCHER_NAME + " version: " + BuildConfig.printableVersionString() + "\n\n", MessageLevel::Launcher));
    m_launcher->start();
}

void LaunchController::readyForLaunch()
{
    // profiler system (BaseProfiler) has been removed
    m_launcher->proceed();
}

void LaunchController::onSucceeded()
{
    emitSucceeded();
}

void LaunchController::onFailed(QString reason)
{
    if(m_instance->settings()->get("ShowConsoleOnError").toBool())
    {
        APPLICATION->showInstanceWindow(m_instance, "console");
    }
    emitFailed(reason);
}

void LaunchController::onProgressRequested(Task* task)
{
    ProgressDialog progDialog(m_parentWidget);
    progDialog.setSkipButton(true, tr("Abort"));
    m_launcher->proceed();
    progDialog.execWithTask(task);
}

bool LaunchController::abort()
{
    if(!m_launcher)
    {
        return true;
    }
    if(!m_launcher->canAbort())
    {
        return false;
    }
    auto response = CustomMessageBox::selectable(
            m_parentWidget, tr("Kill Minecraft?"),
            tr("This can cause the instance to get corrupted and should only be used if Minecraft "
            "is frozen for some reason"),
            QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)->exec();
    if (response == QMessageBox::Yes)
    {
        return m_launcher->abort();
    }
    return false;
}
