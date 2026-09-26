#include "ContentView.h"
#include "UI/LassoControls.h"
#include "UI/EffectsSheet.h"
#include "UI/FilterSheet.h"
#include "UI/HueSaturationSheet.h"
#include "UI/LevelsSheet.h"
#include <QFileDialog>
#include <QMessageBox>

void ContentView::synchronizePanels()
{
    // A panel opens with its edit; close and Escape cancel.
    if (m_levelsShown != m_session.levels().has_value()) {
        m_levelsShown = m_session.levels().has_value();
        if (m_levelsShown) {
            m_levelsPanel.onClose = [this] { m_session.cancelLevels(); };
            m_levelsPanel.show(QStringLiteral("Levels"), new LevelsSheet(m_session));
        } else {
            m_levelsPanel.close();
        }
    }
    if (m_hueSaturationShown != m_session.hueSaturation().has_value()) {
        m_hueSaturationShown = m_session.hueSaturation().has_value();
        if (m_hueSaturationShown) {
            m_adjustmentPanel.onClose = [this] { m_session.cancelHueSaturation(); };
            m_adjustmentPanel.show(QStringLiteral("Hue/Saturation"), new HueSaturationSheet(m_session));
        } else {
            m_adjustmentPanel.close();
        }
    }
    // Swift's onChange of the amount's question: a sheet per question.
    if (m_selectionAmountShown != m_session.selectionAmountOperation()) {
        m_selectionAmountShown = m_session.selectionAmountOperation();
        if (m_selectionAmountShown) {
            m_selectionAmountPanel.onClose = [this] { m_session.setSelectionAmountOperation(std::nullopt); };
            m_selectionAmountPanel.show(rawValue(*m_selectionAmountShown) + QStringLiteral(" Selection"),
                                        new SelectionAmountSheet(m_session, *m_selectionAmountShown));
        } else {
            m_selectionAmountPanel.close();
        }
    }
    if (m_filterShown != m_session.filterEdit().has_value()) {
        m_filterShown = m_session.filterEdit().has_value();
        if (m_filterShown) {
            m_filterPanel.onClose = [this] { m_session.cancelFilter(); };
            m_filterPanel.show(rawValue(m_session.filterEdit()->kind), new FilterSheet(m_session));
        } else {
            m_filterPanel.close();
        }
    }
    // Swift's onChange of the layers: a vanished effect's panel goes.
    if (const std::optional<LayerEffectSelection> editing = m_session.effectsEditing()) {
        const std::vector<ImageLayer> &layers = m_session.document() ? m_session.document()->layers : std::vector<ImageLayer>();
        const int index = indexOf(layers, editing->layerID);
        if (index < 0 || !layers[size_t(index)].effects || !layers[size_t(index)].effects->contains(editing->kind)) {
            if (m_session.colorPicker() && m_session.colorPicker()->target.kind == ColorPickerTarget::Kind::effect)
                m_session.closeColorPicker(false);
            m_session.setEffectsEditing(std::nullopt);
            m_session.setEffectsEditingOriginal(std::nullopt);
        }
    }
    // Swift's onChange of effectsEditing: a panel for each choice.
    if (m_effectsShown != m_session.effectsEditing()) {
        m_effectsShown = m_session.effectsEditing();
        if (m_effectsShown) {
            m_effectsPanel.onClose = [this] { m_session.finishEffectsEditing(false); };
            m_effectsPanel.show(rawValue(m_effectsShown->kind), new EffectsSheet(m_session, m_effectsShown->kind));
        } else {
            m_effectsPanel.close();
        }
    }
}

void ContentView::showImporter()
{
    if (!m_session.showsImporter()) {
        // Dismissed from elsewhere, as a cleared binding dismisses Swift's.
        if (QFileDialog *panel = std::exchange(m_importer, nullptr))
            panel->reject();
        return;
    }
    if (m_importer)
        return;
    m_importer = new QFileDialog(this);
    m_importer->setAttribute(Qt::WA_DeleteOnClose);
    m_importer->setFileMode(QFileDialog::ExistingFiles);
    m_importer->setNameFilter(QStringLiteral("Images (*.jpg *.jpeg *.png *.heic *.tif *.tiff)"));
    connect(m_importer, &QDialog::finished, this, [this, panel = m_importer.data()](int result) {
        // Ours no longer: the flag must not reject it again.
        if (m_importer != panel)
            return;
        m_importer = nullptr;
        m_session.setShowsImporter(false);
        if (result == QDialog::Accepted)
            m_session.importImages(panel->selectedUrls());
    });
    m_importer->open();
}

void ContentView::showAlert(QPointer<QMessageBox> &alert, const QString &title, const std::optional<QString> &message,
                            const std::function<void()> &dismiss)
{
    if (!message) {
        if (QMessageBox *shown = std::exchange(alert, nullptr))
            shown->reject();
        return;
    }
    // SwiftUI's alert shows its message as it stands.
    if (alert) {
        alert->setInformativeText(*message);
        return;
    }
    alert = new QMessageBox(this);
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(title);
    alert->setInformativeText(*message);
    alert->addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    connect(alert, &QDialog::finished, this, [&alert, shown = alert.data(), dismiss] {
        if (alert != shown)
            return;
        alert = nullptr;
        dismiss();
    });
    alert->open();
}
