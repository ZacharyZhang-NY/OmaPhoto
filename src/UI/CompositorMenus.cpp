#include "UI/CompositorMenus.h"
#include "IO/RecentProjects.h"
#include "Logging.h"
#include "UI/KeyboardShortcuts.h"
#include <QApplication>
#include <QMenu>

namespace {
// An entry's name: Hue/Saturation is newHueSaturationAdjustment.
QString adjustmentName(AdjustmentKind kind)
{
    return QStringLiteral("new%1Adjustment").arg(rawValue(kind).remove(QLatin1Char(' ')).remove(QLatin1Char('/')).remove(QLatin1Char('&')));
}

// A filter's entry from Swift's title: gaussianBlur.
QString filterName(FilterKind kind)
{
    QString name = rawValue(kind);
    name.remove(QLatin1Char(' ')).remove(QLatin1Char('&'));
    name[0] = name[0].toLower();
    return name;
}
}

CompositorMenus::CompositorMenus(ProjectWorkspace &workspace, QMenuBar &bar, QWidget &window)
    : QObject(&bar), m_workspace(workspace), m_window(window)
{
    QMenu *file = bar.addMenu(QStringLiteral("&File"));
    add(file, QStringLiteral("newCanvas"), QStringLiteral("New Canvas…"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] { projects().newCanvas(); });
    add(file, QStringLiteral("openProject"), QStringLiteral("Open Project…"), QKeySequence(Qt::CTRL | Qt::Key_O), [this] { projects().open(); });
    QMenu *recent = file->addMenu(QStringLiteral("Open Recent"));
    recent->menuAction()->setObjectName(QStringLiteral("openRecent"));
    listRecent(recent);
    connect(&RecentProjects::shared(), &RecentProjects::changed, this, [this, recent] { listRecent(recent); });
    add(file, QStringLiteral("importImages"), QStringLiteral("Import Images…"), QKeySequence(), [this] { session().setShowsImporter(true); });
    file->addSeparator();
    add(file, QStringLiteral("save"), QStringLiteral("Save"), QKeySequence(Qt::CTRL | Qt::Key_S), [this] { projects().save(); });
    add(file, QStringLiteral("saveAs"), QStringLiteral("Save As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] { projects().save(true); });
    file->addSeparator();
    add(file, QStringLiteral("exportPNG"), QStringLiteral("Export PNG…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { projects().exportPNG(); });
    add(file, QStringLiteral("exportJPEG"), QStringLiteral("Export JPEG…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S),
        [this] { projects().exportJPEG(); });
    file->addSeparator();
    add(file, QStringLiteral("closeProject"), QStringLiteral("Close Project"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] { projects().close(&m_window); });

    QMenu *edit = bar.addMenu(QStringLiteral("&Edit"));
    add(edit, QStringLiteral("undo"), QStringLiteral("Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), [this] {
        if (m_field)
            m_field->undo();
        else if (InlineTextEditor *text = typedText())
            text->undo();
        else
            session().undo();
    });
    add(edit, QStringLiteral("redo"), QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] {
        if (m_field)
            m_field->redo();
        else if (InlineTextEditor *text = typedText())
            text->redo();
        else
            session().redo();
    });
    edit->addSeparator();
    // Cut, Copy and Paste check when chosen, as Swift's do.
    add(edit, QStringLiteral("cut"), QStringLiteral("Cut"), QKeySequence(Qt::CTRL | Qt::Key_X), [this] {
        if (m_field)
            m_field->cut();
        else if (InlineTextEditor *text = typedText())
            text->cut();
        else if (session().selection() && session().canCopyPixels())
            session().cutSelection();
        else
            qCWarning(lcApp) << "nothing to cut";
    });
    add(edit, QStringLiteral("copy"), QStringLiteral("Copy"), QKeySequence(Qt::CTRL | Qt::Key_C), [this] {
        if (m_field)
            m_field->copy();
        else if (InlineTextEditor *text = typedText())
            text->copy();
        else if (session().canCopyPixels() || session().canCopyLayer())
            session().copySelection();
        else
            qCWarning(lcApp) << "nothing to copy";
    });
    add(edit, QStringLiteral("copyMerged"), QStringLiteral("Copy Merged"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] { session().copyMergedSelection(); });
    add(edit, QStringLiteral("paste"), QStringLiteral("Paste"), QKeySequence(Qt::CTRL | Qt::Key_V), [this] {
        if (m_field)
            m_field->paste();
        else if (InlineTextEditor *text = typedText())
            text->paste();
        else if (m_workspace.pasteCopiedLayer())
            return;
        else if (session().canPaste())
            session().paste();
        else
            qCWarning(lcApp) << "nothing to paste";
    });
    edit->addSeparator();
    add(edit, QStringLiteral("keyboardShortcuts"), QStringLiteral("Keyboard Shortcuts…"), QKeySequence(), [this] {
        m_shortcutsPanel.onClose = [this] { m_shortcutsPanel.close(); };
        m_shortcutsPanel.show(QStringLiteral("Keyboard Shortcuts"), new KeyboardShortcutsSheet([this] { m_shortcutsPanel.close(); }));
    });
    // Text fields delete the selection, else back from the caret.
    add(edit, QStringLiteral("fillForeground"), QStringLiteral("Fill with Foreground Color"), QKeySequence(Qt::ALT | Qt::Key_Backspace), [this] {
        if (!m_field) {
            session().fillSelection(EditorSession::FillSource::foreground);
            return;
        }
        if (!m_field->hasSelectedText())
            m_field->cursorWordBackward(true);
        m_field->backspace();
    });
    add(edit, QStringLiteral("fillBackground"), QStringLiteral("Fill with Background Color"), QKeySequence(Qt::CTRL | Qt::Key_Backspace), [this] {
        if (!m_field) {
            session().fillSelection(EditorSession::FillSource::background);
            return;
        }
        if (!m_field->hasSelectedText())
            m_field->home(true);
        m_field->backspace();
    });
    add(edit, QStringLiteral("clearSelectionPixels"), QStringLiteral("Clear Selection Pixels"), QKeySequence(), [this] { session().clearSelectedPixels(); });
    add(edit, QStringLiteral("contentAwareFill"), QStringLiteral("Content-Aware Fill…"), QKeySequence(Qt::SHIFT | Qt::Key_Backspace),
        [this] { session().beginFilter(FilterKind::contentAwareFill); });

    QMenu *view = bar.addMenu(QStringLiteral("&View"));
    add(view, QStringLiteral("fit"), QStringLiteral("Fit Canvas"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { session().fit(); });
    add(view, QStringLiteral("actualPixels"), QStringLiteral("Actual Pixels"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { session().zoom(1); });
    // Swift's stops; open text keeps the keys, as NSText does.
    const auto zoomBy = [this](int step) {
        if (!m_field && !(m_canvas && session().textDraft()))
            session().zoomKeyboard(step);
    };
    add(view, QStringLiteral("zoomIn"), QStringLiteral("Zoom In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [zoomBy] { zoomBy(1); });
    add(view, QStringLiteral("zoomOut"), QStringLiteral("Zoom Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [zoomBy] { zoomBy(-1); });
    add(view, QStringLiteral("pixelGrid"), QStringLiteral("Pixel Grid (800% and above)"), QKeySequence(), [this] { session().setShowsPixelGrid(!session().showsPixelGrid()); })
        ->setCheckable(true);
    add(view, QStringLiteral("snap"), QStringLiteral("Snap"), QKeySequence(), [this] { session().setSnappingEnabled(!session().snappingEnabled()); })
        ->setCheckable(true);
    // Ctrl+H: the Move tool's box; the desktop hides apps.
    add(view, QStringLiteral("transformControls"), QStringLiteral("Show Transform Controls"), QKeySequence(Qt::CTRL | Qt::Key_H),
        [this] { session().setShowsTransformControls(!session().showsTransformControls()); })
        ->setCheckable(true);
    // Swift 1.1.7's guides, grid, rulers and Snap To.
    view->addSeparator();
    const auto toggle = [this](QMenu *menu, const QString &name, const QString &text, const QKeySequence &key, bool (EditorSession::*read)() const,
                               void (EditorSession::*write)(bool)) {
        add(menu, name, text, key, [this, read, write] { (session().*write)(!(session().*read)()); })->setCheckable(true);
    };
    QMenu *show = view->addMenu(QStringLiteral("Show"));
    toggle(show, QStringLiteral("showGrid"), QStringLiteral("Grid"), QKeySequence(Qt::CTRL | Qt::Key_Apostrophe), &EditorSession::showsGrid,
           &EditorSession::setShowsGrid);
    toggle(show, QStringLiteral("showGuides"), QStringLiteral("Guides"), QKeySequence(Qt::CTRL | Qt::Key_Semicolon), &EditorSession::showsGuides,
           &EditorSession::setShowsGuides);
    add(view, QStringLiteral("gridSettings"), QStringLiteral("Grid Settings…"), QKeySequence(), [this] { projects().gridSettings(); });
    toggle(view, QStringLiteral("showRulers"), QStringLiteral("Rulers"), QKeySequence(Qt::CTRL | Qt::Key_R), &EditorSession::showsRulers,
           &EditorSession::setShowsRulers);
    view->addSeparator();
    toggle(view, QStringLiteral("snapEnabled"), QStringLiteral("Snap"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon),
           &EditorSession::snapEnabled, &EditorSession::setSnapEnabled);
    QMenu *snapTo = view->addMenu(QStringLiteral("Snap To"));
    toggle(snapTo, QStringLiteral("snapToGuides"), QStringLiteral("Guides"), QKeySequence(), &EditorSession::snapToGuides, &EditorSession::setSnapToGuides);
    toggle(snapTo, QStringLiteral("snapToGrid"), QStringLiteral("Grid"), QKeySequence(), &EditorSession::snapToGrid, &EditorSession::setSnapToGrid);
    toggle(snapTo, QStringLiteral("snapToLayers"), QStringLiteral("Layers"), QKeySequence(), &EditorSession::snapToLayers, &EditorSession::setSnapToLayers);
    toggle(snapTo, QStringLiteral("snapToDocumentBounds"), QStringLiteral("Document Bounds"), QKeySequence(), &EditorSession::snapToDocumentBounds,
           &EditorSession::setSnapToDocumentBounds);
    view->addSeparator();
    toggle(view, QStringLiteral("lockGuides"), QStringLiteral("Lock Guides"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon),
           &EditorSession::locksGuides, &EditorSession::setLocksGuides);
    add(view, QStringLiteral("clearGuides"), QStringLiteral("Clear Guides"), QKeySequence(), [this] { session().clearGuides(); });

    QMenu *select = bar.addMenu(QStringLiteral("&Select"));
    // The focus takes Select All first, as Swift's responder chain.
    add(select, QStringLiteral("selectAll"), QStringLiteral("All"), QKeySequence(Qt::CTRL | Qt::Key_A), [this] {
        if (m_field)
            m_field->selectAll();
        else if (InlineTextEditor *text = typedText())
            text->selectAll();
        else
            session().selectAll();
    });
    add(select, QStringLiteral("deselect"), QStringLiteral("Deselect"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { session().deselect(); });
    add(select, QStringLiteral("inverse"), QStringLiteral("Inverse"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this] { session().invertSelection(); });
    add(select, QStringLiteral("layerPixels"), QStringLiteral("Layer's Pixels"), QKeySequence(), [this] {
        if (const std::optional<QUuid> id = session().activeLayerID())
            session().loadLayerSelection(*id);
    });
    add(select, QStringLiteral("subject"), QStringLiteral("Subject"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A),
        [this] { session().selectSubject(SelectionMode::replace, {}); });
    add(select, QStringLiteral("colorRange"), QStringLiteral("Color Range…"), QKeySequence(), [this] { session().beginColorRange(); });
    add(select, QStringLiteral("maskBlackAreas"), QStringLiteral("Mask's Black Areas"), QKeySequence(), [this] {
        if (const std::optional<QUuid> id = session().activeLayerID())
            session().loadMaskSelection(*id);
    });
    select->addSeparator();
    add(select, QStringLiteral("expandSelection"), QStringLiteral("Expand…"), QKeySequence(),
        [this] { session().promptSelectionAmount(SelectionAmountOperation::expand); });
    add(select, QStringLiteral("contractSelection"), QStringLiteral("Contract…"), QKeySequence(),
        [this] { session().promptSelectionAmount(SelectionAmountOperation::contract); });
    add(select, QStringLiteral("featherSelection"), QStringLiteral("Feather…"), QKeySequence(),
        [this] { session().promptSelectionAmount(SelectionAmountOperation::feather); });

    QMenu *image = bar.addMenu(QStringLiteral("&Image"));
    add(image, QStringLiteral("curves"), QStringLiteral("Curves…"), QKeySequence(Qt::CTRL | Qt::Key_M), [this] { session().beginFilter(FilterKind::curves); });
    add(image, QStringLiteral("levels"), QStringLiteral("Levels…"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { session().beginLevels(); });
    add(image, QStringLiteral("hueSaturation"), QStringLiteral("Hue/Saturation…"), QKeySequence(Qt::CTRL | Qt::Key_U),
        [this] { session().beginHueSaturation(); });
    for (const FilterKind kind : {FilterKind::blackWhite, FilterKind::colorBalance, FilterKind::exposure, FilterKind::gradientMap, FilterKind::grain})
        add(image, filterName(kind), rawValue(kind).replace(QLatin1Char('&'), QStringLiteral("&&")) + QStringLiteral("…"), QKeySequence(), [this, kind] { session().beginFilter(kind); });
    add(image, QStringLiteral("invert"), QStringLiteral("Invert"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] { session().invertPixels(); });
    image->addSeparator();
    add(image, QStringLiteral("canvasSize"), QStringLiteral("Canvas Size…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), [this] { projects().canvasSize(); });
    add(image, QStringLiteral("imageSize"), QStringLiteral("Image Size…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I), [this] { projects().imageSize(); });
    add(image, QStringLiteral("trim"), QStringLiteral("Trim…"), QKeySequence(), [this] { projects().trim(); });
    image->addSeparator();
    add(image, QStringLiteral("flipCanvasHorizontal"), QStringLiteral("Flip Canvas Horizontal"), QKeySequence(), [this] { session().flipCanvas(true); });
    add(image, QStringLiteral("flipCanvasVertical"), QStringLiteral("Flip Canvas Vertical"), QKeySequence(), [this] { session().flipCanvas(false); });

    QMenu *filter = bar.addMenu(QStringLiteral("Fil&ter"));
    for (const FilterKind kind : allFilterKinds) {
        if (kind != FilterKind::contentAwareFill && !isImageAdjustment(kind))
            add(filter, filterName(kind), rawValue(kind) + QStringLiteral("…"), QKeySequence(), [this, kind] { session().beginFilter(kind); });
    }

    QMenu *layer = bar.addMenu(QStringLiteral("&Layer"));
    QMenu *adjustments = layer->addMenu(QStringLiteral("New Adjustment Layer"));
    adjustments->menuAction()->setObjectName(QStringLiteral("newAdjustmentLayer"));
    for (const AdjustmentKind kind : allAdjustmentKinds)
        // A menu reads a lone ampersand as a mnemonic: doubled.
        add(adjustments, adjustmentName(kind), rawValue(kind).replace(QLatin1Char('&'), QStringLiteral("&&")) + (isEditable(kind) ? QStringLiteral("…") : QString()), QKeySequence(),
            [this, kind] { session().addAdjustment(kind); });
    add(layer, QStringLiteral("editAdjustment"), QStringLiteral("Edit Adjustment…"), QKeySequence(),
        [this] { session().setAdjustmentEditingID(session().activeLayerID()); });
    layer->addSeparator();
    // Swift's transformCommand: with selected pixels, Transform Selection.
    add(layer, QStringLiteral("transformLayer"), QStringLiteral("Transform Layer"), QKeySequence(Qt::CTRL | Qt::Key_T), [this] { session().transformCommand(); });
    // Swift's layerViaCopy: a selection makes it Layer via Copy.
    add(layer, QStringLiteral("layerViaCopy"), QStringLiteral("Duplicate Layer"), QKeySequence(Qt::CTRL | Qt::Key_J), [this] { session().layerViaCopy(); });
    layer->addSeparator();
    add(layer, QStringLiteral("clippingMask"), QStringLiteral("Create Clipping Mask"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G), [this] {
        if (const std::optional<QUuid> id = session().activeLayerID())
            session().toggleClippingMask(*id);
    });
    layer->addSeparator();
    add(layer, QStringLiteral("groupLayers"), QStringLiteral("Group Selected Layers"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { session().groupSelectedLayers(); });
    add(layer, QStringLiteral("ungroupLayers"), QStringLiteral("Ungroup Layers"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { session().ungroupLayers(); });
    add(layer, QStringLiteral("moveOutOfFolder"), QStringLiteral("Move Out of Folder"), QKeySequence(), [this] { session().moveActiveLayerOutOfGroup(); });
    add(layer, QStringLiteral("newBlankLayer"), QStringLiteral("New Blank Layer"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { session().addBlankLayer(); });
    add(layer, QStringLiteral("renameLayer"), QStringLiteral("Rename Layer…"), QKeySequence(), [this] { session().setRenamingLayerID(session().activeLayerID()); });
    add(layer, QStringLiteral("layerVisibility"), QStringLiteral("Hide Layer"), QKeySequence(), [this] {
        if (const std::optional<QUuid> id = session().activeLayerID())
            session().toggleLayerVisibility(*id);
    });
    layer->addSeparator();
    add(layer, QStringLiteral("moveLayerUp"), QStringLiteral("Move Layer Up"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] { session().moveActiveLayer(1); });
    add(layer, QStringLiteral("moveLayerDown"), QStringLiteral("Move Layer Down"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] { session().moveActiveLayer(-1); });
    add(layer, QStringLiteral("mergeLayers"), QStringLiteral("Merge Down"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { session().mergeLayers(); });
    layer->addSeparator();
    add(layer, QStringLiteral("flipHorizontal"), QStringLiteral("Flip Layer Horizontal"), QKeySequence(), [this] { session().flipLayers(true); });
    add(layer, QStringLiteral("flipVertical"), QStringLiteral("Flip Layer Vertical"), QKeySequence(), [this] { session().flipLayers(false); });
    layer->addSeparator();
    add(layer, QStringLiteral("deleteLayer"), QStringLiteral("Delete Layer"), QKeySequence(), [this] { session().deleteLayerOrMask(); });

    connect(&m_workspace, &ProjectWorkspace::changed, this, &CompositorMenus::watchFront);
    // A field gaining or losing focus changes Undo's meaning.
    connect(qApp, &QApplication::focusChanged, this, &CompositorMenus::focusMoved);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &CompositorMenus::remap);
    watchFront();
}

void CompositorMenus::listRecent(QMenu *recent)
{
    recent->clear();
    for (const QString &path : RecentProjects::shared().paths())
        // A name's ampersand is literal, no mnemonic.
        connect(recent->addAction(ProjectTab::nameWithoutSuffix(path).replace(QLatin1Char('&'), QStringLiteral("&&"))), &QAction::triggered, this, [this, path] { projects().open(path); });
    recent->addSeparator();
    QAction *clear = recent->addAction(QStringLiteral("Clear Menu"));
    clear->setObjectName(QStringLiteral("clearRecent"));
    clear->setEnabled(!RecentProjects::shared().paths().isEmpty());
    connect(clear, &QAction::triggered, this, [] { RecentProjects::shared().clear(); });
}

QAction *CompositorMenus::add(QMenu *menu, const QString &name, const QString &text, const QKeySequence &shortcut, const std::function<void()> &run)
{
    QAction *made = menu->addAction(text);
    made->setObjectName(name);
    made->setProperty("originalShortcut", shortcut);
    made->setShortcut(ShortcutSettings::shared().menu(shortcut));
    connect(made, &QAction::triggered, this, run);
    return made;
}

void CompositorMenus::remap()
{
    for (QAction *entry : parent()->findChildren<QAction *>()) {
        const QVariant original = entry->property("originalShortcut");
        if (original.isValid())
            entry->setShortcut(ShortcutSettings::shared().menu(original.value<QKeySequence>()));
    }
}

QAction *CompositorMenus::action(const QString &name) const
{
    return parent()->findChild<QAction *>(name);
}

void CompositorMenus::watchFront()
{
    // The front tab's session drives every entry.
    if (m_watchedTab != std::optional(m_workspace.current().id)) {
        m_watchedTab = m_workspace.current().id;
        disconnect(m_sessionWatch);
        m_sessionWatch = connect(&session(), &EditorSession::changed, this, &CompositorMenus::synchronize);
    }
    synchronize();
}

void CompositorMenus::focusMoved(QWidget *, QWidget *to)
{
    // The menu bar borrows focus; the field keeps Undo.
    if (qobject_cast<QMenuBar *>(to) == nullptr) {
        m_field = qobject_cast<QLineEdit *>(to);
        m_canvas = qobject_cast<CanvasView *>(to);
    }
    synchronize();
}

void CompositorMenus::synchronize()
{
    EditorSession &s = session();
    ProjectController &p = projects();
    const std::optional<ImageLayer> active = s.activeLayer();
    const bool drawn = s.document().has_value();
    // Swift's text branch: open text makes the entries bare.
    const bool typing = m_field != nullptr || s.textDraft().has_value();
    action(QStringLiteral("newCanvas"))->setEnabled(p.canStart());
    action(QStringLiteral("openProject"))->setEnabled(p.canStart());
    action(QStringLiteral("openRecent"))->setEnabled(p.canStart());
    action(QStringLiteral("importImages"))->setEnabled(!s.levels() && !s.showsBusy() && !s.isImporting() && !s.showsNewDocument());
    action(QStringLiteral("save"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("saveAs"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("exportPNG"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("exportJPEG"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("closeProject"))->setEnabled(p.canStart());
    // A step's ampersand, doubled: a lone one is a mnemonic.
    const auto shown = [](QString name) { return name.replace(QLatin1Char('&'), QStringLiteral("&&")); };
    // In a field, Undo is the field's and bare.
    action(QStringLiteral("undo"))->setText(!typing && s.canUndo() ? QStringLiteral("Undo %1").arg(shown(s.history.undoName())) : QStringLiteral("Undo"));
    action(QStringLiteral("undo"))->setEnabled(typing || s.canUndo());
    action(QStringLiteral("redo"))->setText(!typing && s.canRedo() ? QStringLiteral("Redo %1").arg(shown(s.history.redoName())) : QStringLiteral("Redo"));
    action(QStringLiteral("redo"))->setEnabled(typing || s.canRedo());
    for (const char *name : {"fit", "actualPixels", "zoomIn", "zoomOut"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("pixelGrid"))->setChecked(s.showsPixelGrid());
    action(QStringLiteral("snap"))->setChecked(s.snappingEnabled());
    const bool opened = s.document().has_value();
    for (const auto &[name, value] : std::initializer_list<std::pair<const char *, bool>>{
             {"showGrid", s.showsGrid()}, {"showGuides", s.showsGuides()}, {"showRulers", s.showsRulers()}, {"snapEnabled", s.snapEnabled()},
             {"snapToGuides", s.snapToGuides()}, {"snapToGrid", s.snapToGrid()}, {"snapToLayers", s.snapToLayers()},
             {"snapToDocumentBounds", s.snapToDocumentBounds()}, {"lockGuides", s.locksGuides()}}) {
        action(QString::fromLatin1(name))->setChecked(value);
        action(QString::fromLatin1(name))->setEnabled(opened);
    }
    action(QStringLiteral("clearGuides"))->setEnabled(s.canClearGuides());
    action(QStringLiteral("gridSettings"))->setEnabled(opened);
    action(QStringLiteral("transformControls"))->setChecked(s.showsTransformControls());
    action(QStringLiteral("transformControls"))->setEnabled(s.tool() == NavigationTool::move && s.document().has_value());
    const bool selected = s.selection().has_value();
    action(QStringLiteral("deselect"))->setEnabled(selected && s.canEditSelection());
    action(QStringLiteral("inverse"))->setEnabled(selected && s.canEditSelection());
    action(QStringLiteral("layerPixels"))->setEnabled(active && active->asset && s.canEditSelection());
    action(QStringLiteral("maskBlackAreas"))->setEnabled(active && active->mask && s.canEditSelection());
    action(QStringLiteral("subject"))->setEnabled(s.canSelectSubject());
    action(QStringLiteral("colorRange"))->setEnabled(s.canSelectColorRange());
    for (const char *name : {"expandSelection", "contractSelection", "featherSelection"})
        action(QString::fromLatin1(name))->setEnabled(s.canModifySelection());
    action(QStringLiteral("levels"))->setEnabled(s.canAdjustColors() && !s.hueSaturation());
    for (const FilterKind kind : allFilterKinds) {
        if (kind != FilterKind::contentAwareFill)
            action(filterName(kind))->setEnabled((kind == FilterKind::vignette ? s.canVignette() : s.canAdjustColors()) && !s.hueSaturation());
    }
    action(QStringLiteral("hueSaturation"))->setEnabled(s.canAdjustColors());
    action(QStringLiteral("invert"))->setText(s.isMaskSelected() ? QStringLiteral("Invert Mask") : QStringLiteral("Invert"));
    action(QStringLiteral("invert"))->setEnabled(s.canInvert());
    action(QStringLiteral("canvasSize"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("imageSize"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("trim"))->setEnabled(drawn && p.canStart());
    action(QStringLiteral("flipCanvasHorizontal"))->setEnabled(s.canEditLayers());
    action(QStringLiteral("flipCanvasVertical"))->setEnabled(s.canEditLayers());
    action(QStringLiteral("newAdjustmentLayer"))->setEnabled(s.canEditLayers());
    // Beyond Swift: Invert has no editor, so nothing to open.
    action(QStringLiteral("editAdjustment"))->setEnabled(s.canEditLayers() && active && active->adjustment && isEditable(active->adjustment->kind));
    action(QStringLiteral("transformLayer"))->setText(s.canTransformSelection() ? QStringLiteral("Transform Selection") : QStringLiteral("Transform Layer"));
    action(QStringLiteral("transformLayer"))->setEnabled(s.canTransform() || s.canTransformSelection());
    action(QStringLiteral("clippingMask"))->setText(active && active->maskSourceID ? QStringLiteral("Release Clipping Mask") : QStringLiteral("Create Clipping Mask"));
    action(QStringLiteral("clippingMask"))->setEnabled(s.activeLayerID() && s.canToggleClippingMask(*s.activeLayerID()));
    action(QStringLiteral("groupLayers"))->setEnabled(s.canEditLayers());
    action(QStringLiteral("ungroupLayers"))->setEnabled(s.canUngroupLayers());
    action(QStringLiteral("moveOutOfFolder"))->setEnabled(s.canEditLayers() && active && active->parentID);
    action(QStringLiteral("newBlankLayer"))->setEnabled(s.canEditLayers());
    action(QStringLiteral("renameLayer"))->setEnabled(s.canEditLayers() && active);
    action(QStringLiteral("layerVisibility"))->setText(active && !active->isVisible ? QStringLiteral("Show Layer") : QStringLiteral("Hide Layer"));
    action(QStringLiteral("layerVisibility"))->setEnabled(s.canEditLayers() && active);
    action(QStringLiteral("moveLayerUp"))->setEnabled(s.canMoveActiveLayer(1));
    action(QStringLiteral("moveLayerDown"))->setEnabled(s.canMoveActiveLayer(-1));
    action(QStringLiteral("copyMerged"))->setEnabled(s.canCopyMerged());
    action(QStringLiteral("contentAwareFill"))->setEnabled(s.canContentAwareFill());
    action(QStringLiteral("fillForeground"))->setEnabled(s.canEditPixels());
    action(QStringLiteral("fillBackground"))->setEnabled(s.canEditPixels());
    action(QStringLiteral("clearSelectionPixels"))->setEnabled(s.selection().has_value() && s.canEditPixels());
    action(QStringLiteral("layerViaCopy"))->setText(s.selection() ? QStringLiteral("Layer via Copy") : QStringLiteral("Duplicate Layer"));
    action(QStringLiteral("layerViaCopy"))->setEnabled(s.canCopyPixels() || (!s.selection() && s.canEditLayers() && active));
    action(QStringLiteral("mergeLayers"))->setText(s.mergeTitle());
    action(QStringLiteral("mergeLayers"))->setEnabled(s.canMergeLayers());
    action(QStringLiteral("flipHorizontal"))->setEnabled(s.canTransform());
    action(QStringLiteral("flipVertical"))->setEnabled(s.canTransform());
    const std::optional<LayerEffectSelection> effect = s.selectedEffect();
    action(QStringLiteral("deleteLayer"))->setText(effect ? QStringLiteral("Delete ") + rawValue(effect->kind)
                                                   : s.isMaskSelected() && active && active->mask ? QStringLiteral("Delete Layer Mask")
                                                   : s.selectedLayerIDs().size() > 1 ? QStringLiteral("Delete Layers")
                                                                                     : QStringLiteral("Delete Layer"));
    action(QStringLiteral("deleteLayer"))->setEnabled(s.canEditLayers() && active);
}
