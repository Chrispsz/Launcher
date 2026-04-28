#include "FMLLibrariesTask.h"

#include "FileSystem.h"
#include "minecraft/VersionFilterData.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

#include "BuildConfig.h"
#include "Application.h"

FMLLibrariesTask::FMLLibrariesTask(MinecraftInstance * inst)
{
    m_inst = inst;
}
void FMLLibrariesTask::executeTask()
{
    // Get the mod list
    MinecraftInstance *inst = (MinecraftInstance *)m_inst;
    auto components = inst->getPackProfile();
    auto profile = components->getProfile();

    if (!profile->hasTrait("legacyFML"))
    {
        emitSucceeded();
        return;
    }

    QString version = components->getComponentVersion("net.minecraft");
    auto &fmlLibsMapping = g_VersionFilterData.fmlLibsMapping;
    if (!fmlLibsMapping.contains(version))
    {
        emitSucceeded();
        return;
    }

    auto &libList = fmlLibsMapping[version];

    // determine if we need some libs for FML or forge
    setStatus(tr("Verificando bibliotecas do FML..."));
    if(!components->getComponent("net.minecraftforge"))
    {
        emitSucceeded();
        return;
    }

    // now check the lib folder inside the instance for files.
    for (auto &lib : libList)
    {
        QFileInfo libInfo(FS::PathCombine(inst->libDir(), lib.filename));
        if (libInfo.exists())
            continue;
        fmlLibsToProcess.append(lib);
    }

    // if everything is in place, there's nothing to do here...
    if (fmlLibsToProcess.isEmpty())
    {
        emitSucceeded();
        return;
    }

    // download missing libs to our place
    setStatus(tr("Baixando bibliotecas do FML..."));
    auto dljob = new NetJob("FML libraries", APPLICATION->network());
    auto metacache = APPLICATION->metacache();
    for (auto &lib : fmlLibsToProcess)
    {
        auto entry = metacache->resolveEntry("fmllibs", lib.filename);
        QString urlString = BuildConfig.FMLLIBS_BASE_URL + lib.filename;
        dljob->addNetAction(Net::Download::makeCached(QUrl(urlString), entry));
    }

    connect(dljob, &NetJob::succeeded, this, &FMLLibrariesTask::fmllibsFinished);
    connect(dljob, &NetJob::failed, this, &FMLLibrariesTask::fmllibsFailed);
    connect(dljob, &NetJob::progress, this, &FMLLibrariesTask::progress);
    downloadJob.reset(dljob);
    downloadJob->start();
}

bool FMLLibrariesTask::canAbort() const
{
    return true;
}

void FMLLibrariesTask::fmllibsFinished()
{
    downloadJob.reset();
    if (!fmlLibsToProcess.isEmpty())
    {
        setStatus(tr("Copiando bibliotecas do FML para a instância..."));
        MinecraftInstance *inst = (MinecraftInstance *)m_inst;
        auto metacache = APPLICATION->metacache();
        int index = 0;
        for (auto &lib : fmlLibsToProcess)
        {
            progress(index, fmlLibsToProcess.size());
            auto entry = metacache->resolveEntry("fmllibs", lib.filename);
            auto path = FS::PathCombine(inst->libDir(), lib.filename);
            if (!FS::ensureFilePathExists(path))
            {
                emitFailed(tr("Falha ao criar pasta de bibliotecas do FML dentro da instância."));
                return;
            }
            if (!QFile::copy(entry->getFullPath(), FS::PathCombine(inst->libDir(), lib.filename)))
            {
                emitFailed(tr("Falha ao copiar biblioteca do Forge/FML: %1.").arg(lib.filename));
                return;
            }
            index++;
        }
        progress(index, fmlLibsToProcess.size());
    }
    emitSucceeded();
}
void FMLLibrariesTask::fmllibsFailed(QString reason)
{
    if(!downloadJob) {
        emitFailed(reason);
        return;
    }
    QStringList failed = downloadJob->getFailedFiles();
    QString failed_all = failed.join("\n");
    emitFailed(tr("Falha ao baixar os seguintes arquivos:\n%1\n\nMotivo:%2\nTente novamente.").arg(failed_all, reason));
}

bool FMLLibrariesTask::abort()
{
    if(downloadJob)
    {
        return downloadJob->abort();
    }
    else
    {
        qWarning() << "Prematurely aborted FMLLibrariesTask";
    }
    return true;
}
