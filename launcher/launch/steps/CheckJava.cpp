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

#include "CheckJava.h"
#include <launch/LaunchTask.h>
#include <FileSystem.h>
#include <QStandardPaths>
#include <QFileInfo>
#include <sys.h>

void CheckJava::executeTask()
{
    auto instance = m_parent->instance();
    auto settings = instance->settings();
    m_javaPath = FS::ResolveExecutable(settings->get("JavaPath").toString());
    bool perInstance = settings->get("OverrideJava").toBool() || settings->get("OverrideJavaLocation").toBool();

    auto realJavaPath = QStandardPaths::findExecutable(m_javaPath);
    if (realJavaPath.isEmpty())
    {
        if (perInstance)
        {
            emit logLine(
                tr("O binário Java \"%1\" não foi encontrado. Corrija o caminho do Java "
                   "nas configurações da instância ou desative a substituição.").arg(m_javaPath),
                MessageLevel::Warning);
        }
        else
        {
            emit logLine(tr("O binário Java \"%1\" não foi encontrado. Configure o Java nas configurações.").arg(m_javaPath),
                         MessageLevel::Warning);
        }
        emitFailed(tr("Caminho do Java inválido."));
        return;
    }
    else
    {
        emit logLine(tr("Caminho do Java:\n%1\n\n").arg(m_javaPath), MessageLevel::Launcher);
    }

    QFileInfo javaInfo(realJavaPath);
    qlonglong javaUnixTime = javaInfo.lastModified().toMSecsSinceEpoch();
    auto storedUnixTime = settings->get("JavaTimestamp").toLongLong();
    auto storedArchitecture = settings->get("JavaArchitecture").toString();
    auto storedVersion = settings->get("JavaVersion").toString();
    auto storedVendor = settings->get("JavaVendor").toString();
    m_javaUnixTime = javaUnixTime;
    // if timestamps are not the same, or something is missing, check!
    if (javaUnixTime != storedUnixTime || storedVersion.size() == 0 || storedArchitecture.size() == 0 || storedVendor.size() == 0)
    {
        m_JavaChecker = new JavaChecker();
        emit logLine(tr("Verificando versão do Java..."), MessageLevel::Launcher);
        connect(m_JavaChecker.get(), &JavaChecker::checkFinished, this, &CheckJava::checkJavaFinished);
        m_JavaChecker->m_path = realJavaPath;
        m_JavaChecker->performCheck();
        return;
    }
    else
    {
        auto verString = instance->settings()->get("JavaVersion").toString();
        auto archString = instance->settings()->get("JavaArchitecture").toString();
        auto vendorString = instance->settings()->get("JavaVendor").toString();
        printJavaInfo(verString, archString, vendorString);
    }
    emitSucceeded();
}

void CheckJava::checkJavaFinished(JavaCheckResult result)
{
    switch (result.validity)
    {
        case JavaCheckResult::Validity::Errored:
        {
            emit logLine(tr("Não foi possível iniciar o Java:"), MessageLevel::Error);
            emit logLines(result.errorLog.split('\n'), MessageLevel::Error);
            emit logLine(tr("\nVerifique as configurações de Java do launcher."), MessageLevel::Launcher);
            printSystemInfo(false, false);
            emitFailed(tr("Não foi possível iniciar o Java!"));
            return;
        }
        case JavaCheckResult::Validity::ReturnedInvalidData:
        {
            emit logLine(tr("O verificador de Java retornou dados inválidos:"), MessageLevel::Error);
            emit logLines(result.outLog.split('\n'), MessageLevel::Warning);
            emit logLine(tr("\nO Minecraft pode não iniciar corretamente."), MessageLevel::Launcher);
            printSystemInfo(false, false);
            emitSucceeded();
            return;
        }
        case JavaCheckResult::Validity::Valid:
        {
            auto instance = m_parent->instance();
            printJavaInfo(result.javaVersion.toString(), result.mojangPlatform, result.javaVendor);
            instance->settings()->set("JavaVersion", result.javaVersion.toString());
            instance->settings()->set("JavaArchitecture", result.mojangPlatform);
            instance->settings()->set("JavaVendor", result.javaVendor);
            instance->settings()->set("JavaTimestamp", m_javaUnixTime);
            emitSucceeded();
            return;
        }
    }
}

void CheckJava::printJavaInfo(const QString& version, const QString& architecture, const QString & vendor)
{
    emit logLine(tr("Java versão %1, arquitetura %2-bit, de %3.\n\n").arg(version, architecture, vendor), MessageLevel::Launcher);
    printSystemInfo(true, architecture == "64");
}

void CheckJava::printSystemInfo(bool javaIsKnown, bool javaIs64bit)
{
    auto cpu64 = Sys::isCPU64bit();
    auto system64 = Sys::isSystem64bit();
    if(cpu64 != system64)
    {
        emit logLine(tr("A arquitetura da CPU não corresponde à arquitetura do sistema. Considere instalar um sistema operacional de 64 bits.\n\n"), MessageLevel::Error);
    }
    if(javaIsKnown)
    {
        if(javaIs64bit != system64)
        {
            emit logLine(tr("A arquitetura do Java não corresponde à arquitetura do sistema. Considere instalar uma versão do Java de 64 bits.\n\n"), MessageLevel::Error);
        }
    }
}
