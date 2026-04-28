/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "AccountTask.h"
#include "MinecraftAccount.h"

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QByteArray>

#include <QDebug>

AccountTask::AccountTask(AccountData *data, QObject *parent)
    : Task(parent), m_data(data)
{
    changeState(AccountTaskState::STATE_CREATED);
}

QString AccountTask::getStateMessage() const
{
    switch (m_taskState)
    {
    case AccountTaskState::STATE_CREATED:
        return "Waiting...";
    case AccountTaskState::STATE_WORKING:
        return tr("Enviando requisição para os servidores de autenticação...");
    case AccountTaskState::STATE_SUCCEEDED:
        return tr("Tarefa de autenticação concluída com sucesso.");
    case AccountTaskState::STATE_OFFLINE:
        return tr("Falha ao contatar o servidor de autenticação.");
    case AccountTaskState::STATE_FAILED_SOFT:
        return tr("Erro encontrado durante a autenticação.");
    case AccountTaskState::STATE_FAILED_MUST_MIGRATE:
        return tr("Falha na autenticação. A conta deve ser migrada para uma conta Microsoft para ser utilizada.");
    case AccountTaskState::STATE_FAILED_HARD:
        return tr("Falha na autenticação. A sessão expirou.");
    case AccountTaskState::STATE_FAILED_GONE:
        return tr("Falha na autenticação. A conta não existe mais.");
    default:
        return tr("...");
    }
}

bool AccountTask::changeState(AccountTaskState newState, QString reason)
{
    m_taskState = newState;
    setStatus(getStateMessage());
    switch(newState) {
        case AccountTaskState::STATE_CREATED: {
            m_data->errorString.clear();
            return true;
        }
        case AccountTaskState::STATE_WORKING: {
            m_data->accountState = AccountState::Working;
            return true;
        }
        case AccountTaskState::STATE_SUCCEEDED: {
            m_data->accountState = AccountState::Online;
            emitSucceeded();
            return false;
        }
        case AccountTaskState::STATE_OFFLINE: {
            m_data->errorString = reason;
            m_data->accountState = AccountState::Offline;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_SOFT: {
            m_data->errorString = reason;
            m_data->accountState = AccountState::Errored;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_MUST_MIGRATE: {
            m_data->errorString = reason;
            m_data->accountState = AccountState::MustMigrate;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_HARD: {
            m_data->errorString = reason;
            m_data->accountState = AccountState::Expired;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_GONE: {
            m_data->errorString = reason;
            m_data->accountState = AccountState::Gone;
            emitFailed(reason);
            return false;
        }
        default: {
            QString error = tr("Estado de tarefa de conta desconhecido: %1").arg(int(newState));
            m_data->accountState = AccountState::Errored;
            emitFailed(error);
            return false;
        }
    }
}
