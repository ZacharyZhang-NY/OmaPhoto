#pragma once
#include "Document/EditorSession.h"
#include "IO/ProjectStore+Json.h"
#include <QJsonDocument>
#include <QStringList>

// All an observer can read, as text.
inline QStringList described(const EditorSession &session)
{
    const auto text = [](const std::optional<QUuid> &id) { return id.has_value() ? id.value().toString() : QStringLiteral("none"); };
    const auto sorted = [](const QSet<QUuid> &ids) {
        QStringList all;
        for (const QUuid &id : ids)
            all << id.toString();
        all.sort();
        return all.join(' ');
    };
    QStringList result{
        "active " + text(session.activeLayerID()),
        "selected " + sorted(session.selectedLayerIDs()),
        "collapsed " + sorted(session.collapsedGroupIDs()),
        "tool " + QString::number(int(session.tool())),
        QStringLiteral("flags %1%2%3%4").arg(int(session.isProjectBusy())).arg(int(session.showsNewDocument()))
            .arg(int(session.showsImporter())).arg(int(session.isImporting())),
        QStringLiteral("dimmed %1 startable %2").arg(int(session.showsBusy())).arg(int(session.canStartProjectOperation())),
        "error " + session.importError().value_or(QStringLiteral("none")),
        "tool error " + session.brushError().value_or(QStringLiteral("none")),
        "renaming " + text(session.renamingLayerID()),
        "adjusting " + text(session.adjustmentEditingID()) + (session.adjustmentOriginal().has_value() ? " open" : ""),
        "effects " + (session.effectsEditing() ? text(session.effectsEditing().value().layerID) + " " + rawValue(session.effectsEditing().value().kind) : QStringLiteral("closed"))
            + (session.effectsEditingOriginal() ? " " + QJsonDocument(ManifestJson::encoded(session.effectsEditingOriginal().value())).toJson(QJsonDocument::Compact) : QString())
            + " chosen " + (session.effectSelection() ? text(session.effectSelection().value().layerID) + " " + rawValue(session.effectSelection().value().kind) : QStringLiteral("none")),
        "picker " + (session.colorPicker() ? session.colorPicker().value().target.title() + " " + session.colorPicker().value().color().hex() : QStringLiteral("closed")),
        QStringLiteral("mask target %1").arg(int(session.isMaskSelected())),
        QStringLiteral("history %1%2").arg(int(session.canUndo())).arg(int(session.canRedo())),
        QStringLiteral("names %1/%2").arg(session.history.undoName(), session.history.redoName()),
        QStringLiteral("undos %1 modified %2").arg(session.history.undoCount()).arg(int(session.isModified())),
        QStringLiteral("view %1 %2").arg(session.viewport.zoom()).arg(int(session.viewport.followsFit())),
        QStringLiteral("grid %1 focus %2").arg(int(session.showsPixelGrid())).arg(session.canvasFocusRequest()),
    };
    const auto placed = [](const LayerTransform &transform) {
        return QStringLiteral("%1,%2 %3x%4").arg(transform.origin.x()).arg(transform.origin.y()).arg(transform.size.width()).arg(transform.size.height());
    };
    if (session.blendPreview().has_value())
        result << "preview " + session.blendPreview().value().layerID.toString() + " " + rawValue(session.blendPreview().value().mode);
    if (session.transformEdit().has_value()) {
        const TransformEdit &edit = session.transformEdit().value();
        result << "edit " + edit.layerID.toString() + " " + placed(edit.draft) + (edit.group.has_value() ? " group" : "") + (edit.mask ? " mask" : "");
    }
    const BrushSettings &tip = session.brushSettings();
    result << QStringLiteral("brush %1 %2 %3 rgb %4 %5 %6 erase %7 mode %8 white %9 stroke %10 revision %11").arg(tip.diameter).arg(tip.hardness).arg(tip.opacity)
                  .arg(tip.red).arg(tip.green).arg(tip.blue).arg(int(tip.erasing)).arg(rawValue(session.brushMode())).arg(int(session.maskPaintWhite()))
                  .arg(session.brushStroke() ? int(session.brushStroke()->patches().size()) : -1).arg(session.brushRevision());
    result << QStringLiteral("smoothing %1").arg(tip.smoothing);
    if (session.filterEdit().has_value()) {
        const FilterEdit &edit = session.filterEdit().value();
        result << QStringLiteral("filter %1 %2 preview %3 preparing %4 committing %5 error %6 grown %7").arg(rawValue(edit.kind), edit.layerID.toString())
                      .arg(int(edit.preview)).arg(int(edit.preparing)).arg(int(edit.committing)).arg(edit.previewError.value_or(QStringLiteral("none")))
                      .arg(edit.grownTransform.has_value() ? placed(edit.grownTransform.value()) : QStringLiteral("none"));
    }
    if (session.levels().has_value())
        result << QStringLiteral("levels %1 preview %2 ready %3").arg(session.levels().value().id.toString()).arg(int(session.levels().value().preview))
                      .arg(int(session.levels().value().histogramReady));
    if (session.hueSaturation().has_value())
        result << QStringLiteral("hue %1 preview %2").arg(session.hueSaturation().value().id.toString()).arg(int(session.hueSaturation().value().preview));
    const auto styled = [](const LayerTextStyle &style) {
        return QStringLiteral("\"%1\" %2 %3 rgb %4 %5 %6 %7 %8 %9").arg(style.content, style.fontName).arg(style.fontSize).arg(style.red)
                   .arg(style.green).arg(style.blue).arg(rawValue(style.alignment)).arg(style.tracking).arg(style.leading)
               + (style.boxSize.has_value() ? QStringLiteral(" box %1x%2").arg(style.boxSize.value().width()).arg(style.boxSize.value().height()) : QString());
    };
    result << "text defaults " + styled(session.textDefaults());
    if (session.textDraft().has_value()) {
        const TextDraft &draft = session.textDraft().value();
        result << QStringLiteral("text draft %1 for %2 at %3,%4 ").arg(draft.id.toString(), text(draft.layerID)).arg(draft.origin.x()).arg(draft.origin.y())
                + styled(draft.style) + (draft.transform.has_value() ? " placed " + placed(draft.transform.value()) : QString());
    }
    result << "path " + session.projectPath().value_or(QStringLiteral("none"));
    result << QStringLiteral("selection tools %1 %2 mode %3 held %4 soft %5 grow %6 %7").arg(int(session.lassoKind())).arg(int(session.marqueeKind()))
                  .arg(int(session.selectionModeChoice())).arg(session.heldSelectionMode().has_value() ? int(session.heldSelectionMode().value()) : -1)
                  .arg(int(session.selectionAntialiased())).arg(session.selectionExpandAmount()).arg(session.selectionContractAmount());
    result << QStringLiteral("wand %1 %2 %3 %4").arg(session.wandSettings().tolerance).arg(int(session.wandSettings().sampleSize))
                  .arg(int(session.wandSettings().contiguous)).arg(int(session.wandSettings().sampleAllLayers));
    result << QStringLiteral("magic %1 object %2 %3").arg(int(session.wandMode())).arg(int(session.objectSelectionSettings().sampleAllLayers))
                  .arg(session.objectSelectionSettings().edgeOffset);
    if (session.lassoDraft().has_value()) {
        const LassoDraft &draft = session.lassoDraft().value();
        result << QStringLiteral("lasso %1 %2 points %3 cursor %4").arg(int(draft.kind)).arg(int(draft.mode)).arg(draft.points.size())
                      .arg(draft.cursor.has_value() ? QStringLiteral("%1,%2").arg(draft.cursor.value().x()).arg(draft.cursor.value().y()) : QStringLiteral("none"));
    }
    if (session.document().has_value()) {
        const CanvasDocument &canvas = session.document().value();
        result << QStringLiteral("canvas %1 %2x%3 at %4").arg(canvas.id.toString()).arg(canvas.width).arg(canvas.height).arg(canvas.resolution);
        if (canvas.selection.has_value()) {
            const QRectF bounds = canvas.selection.value().path.boundingRect();
            result << QStringLiteral("outline %1,%2 %3x%4 soft %5").arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                          .arg(int(canvas.selection.value().antialiased));
        }
        for (const ImageLayer &layer : session.document().value().layers) {
            result << QStringLiteral("%1 %2 %3 %4 ").arg(layer.id.toString(), layer.name, text(layer.parentID)).arg(int(layer.isVisible))
                    + placed(layer.transform) + QStringLiteral(" %1 ").arg(layer.opacity) + rawValue(layer.blendMode)
                    + " clipped to " + text(layer.maskSourceID)
                    + QStringLiteral(" pixels %1").arg(layer.asset.has_value() ? layer.asset.value().identity().cacheKey : 0)
                    + (layer.text.has_value() ? " text " + styled(layer.text.value().style) + (layer.liveText().has_value() ? " live" : "") : QString())
                    + (layer.adjustment.has_value() ? " adjusts " + QJsonDocument(ManifestJson::encoded(layer.adjustment.value())).toJson(QJsonDocument::Compact)
                                                    : QString())
                    + (layer.effects.has_value() ? " effects " + QJsonDocument(ManifestJson::encoded(layer.effects.value())).toJson(QJsonDocument::Compact) : QString());
            if (layer.mask.has_value()) {
                const LayerMask &mask = layer.mask.value();
                result << QStringLiteral("  mask %1 on %2 linked %3 ").arg(mask.asset.identity().cacheKey).arg(int(mask.isEnabled)).arg(int(mask.isLinked))
                        + (mask.placement.has_value() ? placed(mask.placement.value()) : QStringLiteral("over its layer"));
            }
        }
    }
    return result;
}
