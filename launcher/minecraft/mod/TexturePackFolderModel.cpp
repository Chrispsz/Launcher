#include "TexturePackFolderModel.h"

TexturePackFolderModel::TexturePackFolderModel(const QString &dir) : ModFolderModel(dir) {
}

QVariant TexturePackFolderModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::ToolTipRole) {
        switch (section) {
            case ActiveColumn:
                return tr("O pacote de texturas está ativado?");
            case NameColumn:
                return tr("O nome do pacote de texturas.");
            case VersionColumn:
                return tr("A versão do pacote de texturas.");
            case DateColumn:
                return tr("A data e hora em que este pacote de texturas foi alterado (ou adicionado) pela última vez.");
            default:
                return QVariant();
        }
    }

    return ModFolderModel::headerData(section, orientation, role);
}
