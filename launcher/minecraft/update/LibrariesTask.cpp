#include "LibrariesTask.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

#include "Application.h"

LibrariesTask::LibrariesTask(MinecraftInstance * inst)
{
    m_inst = inst;
}

void LibrariesTask::executeTask()
{
    setStatus(tr("Obtendo os arquivos de bibliotecas do Mojang..."));
    qDebug() << m_inst->name() << ": downloading libraries";
    MinecraftInstance *inst = (MinecraftInstance *)m_inst;

    // Build a list of URLs that will need to be downloaded.
    auto components = inst->getPackProfile();
    auto profile = components->getProfile();

    auto job = new NetJob(tr("Bibliotecas para a instância %1").arg(inst->name()), APPLICATION->network());
    downloadJob.reset(job);

    auto metacache = APPLICATION->metacache();

    auto processArtifactPool = [&](const QList<LibraryPtr> & pool, QStringList & errors, const QString & localPath)
    {
        for (auto lib : pool)
        {
            if(!lib)
            {
                emitFailed(tr("Jar nulo especificado nos metadados, abortando."));
                return false;
            }
            auto dls = lib->getDownloads(currentSystem, metacache.get(), errors, localPath);
            for(auto dl : dls)
            {
                downloadJob->addNetAction(dl);
            }
        }
        return true;
    };

    QStringList failedLocalLibraries;
    QList<LibraryPtr> libArtifactPool;
    libArtifactPool.append(profile->getLibraries());
    libArtifactPool.append(profile->getNativeLibraries());
    libArtifactPool.append(profile->getMavenFiles());
    libArtifactPool.append(profile->getMainJar());
    processArtifactPool(libArtifactPool, failedLocalLibraries, inst->getLocalLibraryPath());

    QStringList failedLocalJarMods;
    processArtifactPool(profile->getJarMods(), failedLocalJarMods, inst->jarModsDir());

    if (!failedLocalJarMods.empty() || !failedLocalLibraries.empty())
    {
        downloadJob.reset();
        QString failed_all = (failedLocalLibraries + failedLocalJarMods).join("\n");
        emitFailed(tr("Alguns artefatos marcados como 'locais' estão sem seus arquivos:\n%1\n\nVocê precisa adicionar os arquivos ou remover os pacotes que os requerem.\nVocê precisará corrigir este problema manualmente.").arg(failed_all));
        return;
    }

    connect(downloadJob.get(), &NetJob::succeeded, this, &LibrariesTask::emitSucceeded);
    connect(downloadJob.get(), &NetJob::failed, this, &LibrariesTask::jarlibFailed);
    connect(downloadJob.get(), &NetJob::progress, this, &LibrariesTask::progress);
    downloadJob->start();
}

bool LibrariesTask::canAbort() const
{
    return true;
}

void LibrariesTask::jarlibFailed(QString reason)
{
    emitFailed(tr("Falha na atualização do jogo: não foi possível obter as bibliotecas necessárias.\nMotivo:\n%1").arg(reason));
}

bool LibrariesTask::abort()
{
    if(downloadJob)
    {
        return downloadJob->abort();
    }
    else
    {
        qWarning() << "Prematurely aborted LibrariesTask";
    }
    return true;
}
