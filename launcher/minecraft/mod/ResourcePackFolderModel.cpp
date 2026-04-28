#include "ResourcePackFolderModel.h"

ResourcePackFolderModel::ResourcePackFolderModel(const QString &dir) : ModFolderModel(dir) {
}

QVariant ResourcePackFolderModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::ToolTipRole) {
        switch (section) {
            case ActiveColumn:
                return tr("O pacote de recursos está ativado?");
            case NameColumn:
                return tr("O nome do pacote de recursos.");
            case VersionColumn:
                return tr("A versão do pacote de recursos.");
            case DateColumn:
                return tr("A data e hora em que este pacote de recursos foi alterado (ou adicionado) pela última vez.");
            default:
                return QVariant();
        }
    }

    return ModFolderModel::headerData(section, orientation, role);
}
