#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyMateEditor.h"
#include "modeling/assembly/MateEquations.h"
#include <QComboBox>
#include <QCursor>
#include <QMenu>
namespace thecad::ui
{
void AssemblyWindow::quickMates()
{
    if (draftActive() || m_preselection.size() != 2)
        return;
    const auto first = m_preselection[0], second = m_preselection[1];
    if (first.reference.componentId == second.reference.componentId)
        return;
    auto* menu = new QMenu(this);
    menu->setObjectName("assemblyQuickMates");
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    using namespace modeling::assembly;
    for (auto kind :
         {MateKind::Coincident, MateKind::Concentric, MateKind::Parallel, MateKind::Perpendicular,
          MateKind::Tangent, MateKind::Lock, MateKind::Distance, MateKind::Angle})
    {
        if (!compatibleMate(kind, first.mateGeometry, second.mateGeometry))
            continue;
        auto* action = menu->addAction(mateName(kind));
        action->setObjectName(QString("quickMate_%1").arg(mateKey(kind)));
        connect(action, &QAction::triggered, this,
                [this, kind]
                {
                    startMate();
                    if (!m_mate)
                        return;
                    auto* types = m_mate->findChild<QComboBox*>("assemblyMateType");
                    types->setCurrentIndex(types->findData(int(kind)));
                    if (kind != MateKind::Distance && kind != MateKind::Angle &&
                        m_mate->candidate())
                        finishMate(true);
                });
    }
    if (menu->isEmpty())
    {
        menu->deleteLater();
        return;
    }
    menu->popup(QCursor::pos());
}
} // namespace thecad::ui
