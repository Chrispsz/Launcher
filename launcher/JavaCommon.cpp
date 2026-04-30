#include "JavaCommon.h"
#include "ui/dialogs/CustomMessageBox.h"
#include <MMCStrings.h>

bool JavaCommon::checkJVMArgs(QString jvmargs, QWidget *parent)
{
    if (jvmargs.contains("-XX:PermSize=") || jvmargs.contains(QRegExp("-Xm[sx]"))
        || jvmargs.contains("-XX-MaxHeapSize") || jvmargs.contains("-XX:InitialHeapSize"))
    {
        auto warnStr = QObject::tr(
            "Você tentou definir manualmente uma opção de memória da JVM (usando \"-XX:PermSize\", \"-XX-MaxHeapSize\", \"-XX:InitialHeapSize\",  \"-Xmx\" ou \"-Xms\").\n"
            "Existem caixas dedicadas para isso nas configurações (aba Java, no grupo Memória no topo).\n"
            "Esta mensagem será exibida até que você as remova dos argumentos da JVM.");
        CustomMessageBox::selectable(
            parent, QObject::tr("Aviso sobre argumentos da JVM"),
            warnStr,
            QMessageBox::Warning)->exec();
        return false;
    }
    // block lunacy with passing required version to the JVM
    if (jvmargs.contains(QRegExp("-version:.*"))) {
        auto warnStr = QObject::tr(
            "Você tentou passar um argumento de versão obrigatória do Java para a JVM (usando \"-version=xxx\"). Isso não é seguro e não será permitido.\n"
            "Esta mensagem será exibida até que você remova isso dos argumentos da JVM.");
        CustomMessageBox::selectable(
            parent, QObject::tr("Aviso sobre argumentos da JVM"),
            warnStr,
            QMessageBox::Warning)->exec();
        return false;
    }
    return true;
}

void JavaCommon::javaWasOk(QWidget *parent, JavaCheckResult result)
{
    QString text;
    text += QObject::tr("Teste de Java bem-sucedido!<br />Plataforma detectada: %1<br />Versão do "
        "Java detectada: %2<br />Fabricante "
        "do Java detectado: %3<br />").arg(result.realPlatform, result.javaVersion.toString(), result.javaVendor);
    if (result.errorLog.size())
    {
        auto htmlError = result.errorLog;
        htmlError.replace('\n', "<br />");
        text += QObject::tr("<br />Avisos:<br /><font color=\"orange\">%1</font>").arg(htmlError);
    }
    CustomMessageBox::selectable(parent, QObject::tr("Teste de Java bem-sucedido"), text, QMessageBox::Information)->show();
}

void JavaCommon::javaArgsWereBad(QWidget *parent, JavaCheckResult result)
{
    auto htmlError = result.errorLog;
    QString text;
    htmlError.replace('\n', "<br />");
    text += QObject::tr("O binário Java especificado não funcionou com os argumentos fornecidos:<br />");
    text += QString("<font color=\"red\">%1</font>").arg(htmlError);
    CustomMessageBox::selectable(parent, QObject::tr("Falha no teste de Java"), text, QMessageBox::Warning)->show();
}

void JavaCommon::javaBinaryWasBad(QWidget *parent, JavaCheckResult result)
{
    QString text;
    text += QObject::tr(
        "O binário Java especificado não funcionou.<br />Você deve usar o recurso de auto-detecção, "
        "ou definir o caminho para o executável do Java.<br />");
    CustomMessageBox::selectable(parent, QObject::tr("Falha no teste de Java"), text, QMessageBox::Warning)->show();
}

void JavaCommon::TestCheck::run()
{
    if (!JavaCommon::checkJVMArgs(m_args, m_parent))
    {
        emit finished();
        return;
    }
    checker.reset(new JavaChecker());
    connect(checker.get(), SIGNAL(checkFinished(JavaCheckResult)), this,
            SLOT(checkFinished(JavaCheckResult)));
    checker->m_path = m_path;
    checker->performCheck();
}

void JavaCommon::TestCheck::checkFinished(JavaCheckResult result)
{
    if (result.validity != JavaCheckResult::Validity::Valid)
    {
        javaBinaryWasBad(m_parent, result);
        emit finished();
        return;
    }
    checker.reset(new JavaChecker());
    connect(checker.get(), SIGNAL(checkFinished(JavaCheckResult)), this,
            SLOT(checkFinishedWithArgs(JavaCheckResult)));
    checker->m_path = m_path;
    checker->m_args = m_args;
    checker->m_minMem = m_minMem;
    checker->m_maxMem = m_maxMem;
    if (result.javaVersion.requiresPermGen())
    {
        checker->m_permGen = m_permGen;
    }
    checker->performCheck();
}

void JavaCommon::TestCheck::checkFinishedWithArgs(JavaCheckResult result)
{
    if (result.validity == JavaCheckResult::Validity::Valid)
    {
        javaWasOk(m_parent, result);
        emit finished();
        return;
    }
    javaArgsWereBad(m_parent, result);
    emit finished();
}

