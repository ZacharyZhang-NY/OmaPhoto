#include "UI/NativeLayerList.h"
#include <QMenu>

// Swift's table-level context menu (1.2.5): entries and validation.
std::unique_ptr<QMenu> NativeLayerList::contextMenu(QUuid id)
{
    EditorSession &session = m_session;
    if (session.selectedLayerIDs().isEmpty())
        session.selectLayer(id);
    auto menu = std::make_unique<QMenu>(this);
    // A refused selection leaves no layer: the gate rests then.
    const std::optional<ImageLayer> active = session.activeLayer();
    const bool editable = session.canEditLayers();
    const bool mask = active && active->mask;
    const auto entry = [&](QMenu &into, const char *name, const QString &title, bool enabled, std::function<void()> run) {
        QAction *action = into.addAction(title, this, std::move(run));
        action->setObjectName(QString::fromLatin1(name));
        action->setEnabled(enabled);
    };
    // Swift's actions read the active layer when chosen.
    const auto onActive = [&session](std::function<void(QUuid)> run) {
        return [&session, run] {
            if (const std::optional<QUuid> activeID = session.activeLayerID())
                run(*activeID);
        };
    };
    entry(*menu, "duplicateLayer", QStringLiteral("Duplicate Layer"), editable, [&session] { session.duplicateActiveLayer(); });
    entry(*menu, "renameLayer", QStringLiteral("Rename…"), editable && session.selectedLayerIDs().size() == 1, onActive([&session](QUuid activeID) {
        if (session.canEditLayers())
            session.setRenamingLayerID(activeID);
    }));
    // A chosen mask exists: Swift's `mask != nil` is dead.
    const QString deleteTitle = session.isMaskSelected()                 ? QStringLiteral("Delete Mask")
                                : session.selectedLayerIDs().size() > 1 ? QStringLiteral("Delete Selected Layers")
                                                                         : QStringLiteral("Delete Layer");
    entry(*menu, "deleteLayer", deleteTitle, editable, [&session] { session.deleteLayerOrMask(); });
    menu->addSeparator();
    entry(*menu, "clippingMask", active && active->maskSourceID ? QStringLiteral("Release Clipping Mask") : QStringLiteral("Create Clipping Mask"),
          active && session.canToggleClippingMask(active->id), onActive([&session](QUuid activeID) { session.toggleClippingMask(activeID); }));
    entry(*menu, "groupLayers", QStringLiteral("Group Selected Layers"),
          session.canEditLayers() && session.document() && session.document()->layers.size() < 10'000 && !session.selectedLayerIDs().isEmpty(),
          [&session] { session.groupSelectedLayers(); });
    entry(*menu, "moveOut", QStringLiteral("Move Out of Folder"), session.canEditLayers() && active && active->parentID,
          [&session] { session.moveActiveLayerOutOfGroup(); });
    entry(*menu, "mergeLayers", session.mergeTitle(), session.canMergeLayers(), [&session] { session.mergeLayers(); });
    menu->addSeparator();
    // The mask entries target the layer's pixels first, as Swift's.
    const bool masking = session.canEditMask();
    const auto onPixels = [&session, onActive](std::function<void()> then) {
        return onActive([&session, then](QUuid activeID) {
            session.selectLayerTarget(activeID, false);
            then();
        });
    };
    QMenu *add = menu->addMenu(QStringLiteral("Add Mask"));
    add->menuAction()->setObjectName(QStringLiteral("addMask"));
    add->menuAction()->setEnabled(masking && !mask);
    entry(*add, "addWhiteMask", QStringLiteral("Reveal All (White)"), masking && !mask, onPixels([&session] { session.addMask(true); }));
    entry(*add, "addBlackMask", QStringLiteral("Hide All (Black)"), masking && !mask, onPixels([&session] { session.addMask(false); }));
    entry(*menu, "toggleMask", mask && !active->mask->isEnabled ? QStringLiteral("Enable Mask") : QStringLiteral("Disable Mask"), masking && mask,
          onPixels([&session] { session.toggleLayerMask(); }));
    entry(*menu, "deleteMask", QStringLiteral("Delete Mask"), masking && mask, onPixels([&session] { session.deleteLayerMask(); }));
    entry(*menu, "linkMask", mask && !active->mask->isLinked ? QStringLiteral("Link Mask") : QStringLiteral("Unlink Mask"),
          session.canEditLayers() && mask && !active->isGroup && !active->adjustment,
          onActive([&session](QUuid activeID) { session.toggleMaskLink(activeID); }));
    menu->addSeparator();
    entry(*menu, "layerVisibility", active && !active->isVisible ? QStringLiteral("Show Layer") : QStringLiteral("Hide Layer"), editable,
          onActive([&session](QUuid activeID) { session.toggleLayerVisibility(activeID); }));
    return menu;
}

// Swift's menu(for:): the press chooses what the menu acts on.
std::unique_ptr<QMenu> NativeLayerList::menuFor(LayerCell &cell, QPoint cellPoint)
{
    // Swift's targetLayer: the session's calls below may rebuild the rows.
    const QUuid id = cell.layerID();
    // Swift's table selects no row while an effect is chosen.
    const bool rowSelected = !m_session.selectedEffect() && m_session.selectedLayerIDs().contains(id);
    QWidget *under = cell.childAt(cellPoint);
    // On an effect's row: that effect is chosen, rows untouched.
    for (QWidget *each = under; each && each != &cell; each = each->parentWidget()) {
        if (auto *effect = qobject_cast<LayerEffectRow *>(each)) {
            effect->select(false);
            return contextMenu(id);
        }
    }
    // Each call below drops a chosen effect itself.
    const auto *thumbnail = qobject_cast<LayerThumbnailButton *>(under);
    const bool maskThumbnail = thumbnail && thumbnail->isMaskTarget, layerThumbnail = thumbnail && !thumbnail->isMaskTarget;
    if (rowSelected) {
        if (maskThumbnail || layerThumbnail)
            m_session.selectLayerTarget(id, maskThumbnail);
        else
            m_session.selectLayers(QSet<QUuid>(m_session.selectedLayerIDs()), id);
    } else {
        m_session.selectLayers({id}, id);
        focusList();
        m_session.selectLayerTarget(id, maskThumbnail);
    }
    return contextMenu(id);
}
