#include "LaunchController.h"
#include "minecraft/auth/AccountList.h"
#include "Application.h"

#include "ui/MainWindow.h"
#include "ui/InstanceWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"


#include <QLineEdit>
#include <QInputDialog>
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
        emitFailed(tr("Nenhuma instância especificada!"));
        return;
    }

    if(!JavaCommon::checkJVMArgs(m_instance->settings()->get("JvmArgs").toString(), m_parentWidget)) {
        emitFailed(tr("Argumentos Java inválidos especificados. Corrija isso primeiro."));
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
            tr("Sem contas"),
            tr("Para jogar Minecraft, você deve ter pelo menos uma conta "
               "conectada."
               "Gostaria de abrir o gerenciador de contas para adicionar uma conta agora?"),
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
        // No default account set - use the first available one
        if (accounts->count() > 0) {
            m_accountToUse = accounts->at(0);
        }
    }
}


void LaunchController::login() {
    decideAccount();

    // if no account is selected, we bail
    if (!m_accountToUse)
    {
        emitFailed(tr("Nenhuma conta selecionada para iniciar."));
        return;
    }

    // we try empty password first :)
    QString password;
    // we loop until the user succeeds in logging in or gives up
    bool tryagain = true;
    // the failure. the default failure.
    const QString needLoginAgain = tr("Sua conta não está logada no momento. Você pode jogar offline.");
    QString failReason = needLoginAgain;

    while (tryagain)
    {
        m_session = std::make_shared<AuthSession>();
        m_session->wants_online = m_online;
        m_accountToUse->fillSession(m_session);

        if (m_accountToUse->typeString() == "local") {
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
                            tr("Nome do jogador"),
                            tr("Escolha seu nome no modo offline."),
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
                    box.setWindowTitle(tr("Jogar demonstração?"));
                    box.setText(tr("Esta conta não possui Minecraft. Você gostaria de jogar a demonstração?"));
                    box.setIcon(QMessageBox::Warning);
                    auto demoButton = box.addButton(tr("Jogar Demonstração"), QMessageBox::ButtonRole::YesRole);
                    auto cancelButton = box.addButton(tr("Cancelar"), QMessageBox::ButtonRole::NoRole);
                    box.setDefaultButton(cancelButton);

                    box.exec();
                    if(box.clickedButton() == demoButton) {
                        // play demo here
                        m_session->MakeDemo();
                        launchInstance();
                    }
                    else {
                        emitFailed(tr("Início cancelado - a conta não possui Minecraft."));
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
                    progDialog.setSkipButton(true, tr("Jogar Offline"));
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
                auto errorString = tr("Esta conta não existe mais nos servidores de autenticação.");
                QMessageBox::warning(
                    m_parentWidget,
                    tr("Conta inexistente"),
                    errorString,
                    QMessageBox::StandardButton::Ok,
                    QMessageBox::StandardButton::Ok
                );
                emitFailed(errorString);
                return;
            }
        }
    }
    emitFailed(tr("Falha ao iniciar."));
}

void LaunchController::launchInstance()
{
    Q_ASSERT_X(m_instance != NULL, "launchInstance", "instance is NULL");
    Q_ASSERT_X(m_session.get() != nullptr, "launchInstance", "session is NULL");

    if(!m_instance->reloadSettings())
    {
        QMessageBox::critical(m_parentWidget, tr("Erro"), tr("Não foi possível carregar o perfil da instância."));
        emitFailed(tr("Não foi possível carregar o perfil da instância."));
        return;
    }

    // Set the profile info on the AuthServer so it can respond to profile lookups
    m_authserver->setProfileInfo(m_session->uuid, m_session->player_name);

    m_launcher = m_instance->createLaunchTask(m_session, m_quickPlayTarget, m_authserver->port());
    if (!m_launcher)
    {
        emitFailed(tr("Não foi possível instanciar o launcher."));
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
    } else {
        online_mode = "offline";
    }

    m_launcher->prependStep(new TextPrint(m_launcher.get(), tr("Instância iniciada no modo %1\n").arg(online_mode), MessageLevel::Launcher));

    // Prepend Version
    m_launcher->prependStep(new TextPrint(m_launcher.get(), BuildConfig.LAUNCHER_NAME + tr(" versão: ") + BuildConfig.printableVersionString() + "\n\n", MessageLevel::Launcher));
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
    progDialog.setSkipButton(true, tr("Abortar"));
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
            m_parentWidget, tr("Matar Minecraft?"),
            tr("Isso pode causar a corrupção da instância e só deve ser usado se o Minecraft "
            "estiver congelado por algum motivo"),
            QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)->exec();
    if (response == QMessageBox::Yes)
    {
        return m_launcher->abort();
    }
    return false;
}
