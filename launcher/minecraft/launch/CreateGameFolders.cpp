#include "CreateGameFolders.h"
#include "minecraft/MinecraftInstance.h"
#include "launch/LaunchTask.h"
#include "FileSystem.h"

CreateGameFolders::CreateGameFolders(LaunchTask* parent): LaunchStep(parent)
{
}

void CreateGameFolders::executeTask()
{
    auto instance = m_parent->instance();
    std::shared_ptr<MinecraftInstance> minecraftInstance = std::dynamic_pointer_cast<MinecraftInstance>(instance);

    if(!FS::ensureFolderPathExists(minecraftInstance->gameRoot()))
    {
        emit logLine(tr("Não foi possível criar a pasta principal do jogo"), MessageLevel::Error);
        emitFailed(tr("Não foi possível criar a pasta principal do jogo"));
        return;
    }

    // HACK: this is a workaround for MCL-3732 - 'server-resource-packs' folder is created.
    if(!FS::ensureFolderPathExists(FS::PathCombine(minecraftInstance->gameRoot(), "server-resource-packs")))
    {
        emit logLine(tr("Não foi possível criar a pasta 'server-resource-packs'"), MessageLevel::Error);
    }
    emitSucceeded();
}
