// Derived from Qt's minimal QPA plugin; LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only.
#include "kindlefb.h"
#include <qpa/qplatformintegrationplugin.h>

QT_BEGIN_NAMESPACE

class KindleIntegrationPlugin final : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "kindlefb.json")
public:
    QPlatformIntegration *create(const QString &system, const QStringList &) override
    {
        if (system.compare(QStringLiteral("kindlefb"), Qt::CaseInsensitive) == 0)
            return new KindleIntegration;
        return nullptr;
    }
};

QT_END_NAMESPACE

#include "plugin.moc"
