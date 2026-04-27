#include "Application.h"

int main(int argc, char *argv[])
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    // initialize Qt
    Application app(argc, argv);

    switch (app.status())
    {
    case Application::StartingUp:
    case Application::Initialized:
    {
        // LAUNCHERMC: Core resources
        Q_INIT_RESOURCE(multimc);
        Q_INIT_RESOURCE(backgrounds);
        Q_INIT_RESOURCE(documents);
        Q_INIT_RESOURCE(logo);

        // LAUNCHERMC: Only load the dark + flat icon themes (others unused)
        Q_INIT_RESOURCE(pe_dark);
        Q_INIT_RESOURCE(flat);
        return app.exec();
    }
    case Application::Failed:
        return 1;
    case Application::Succeeded:
        return 0;
    }
}
