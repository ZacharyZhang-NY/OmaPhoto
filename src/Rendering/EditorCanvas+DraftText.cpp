#include "Document/LayerEffects+Renderer.h"
#include "Logging.h"
#include "Rendering/EditorCanvas.h"
#include "Rendering/InlineTextEditor.h"

// Typed text, drawn as the pixels it commits to.

std::optional<CanvasView::DraftText> CanvasView::draftText()
{
    const std::optional<TextDraft> &draft = m_session.textDraft();
    if (!draft || !m_inlineTextEditor) {
        m_draftTextCache.reset();
        return std::nullopt;
    }
    // Remade only when the style changes.
    if (!m_draftTextCache || m_draftTextCache->style != draft->style) {
        try {
            m_draftTextCache = DraftTextCache{draft->style, EditorSession::textImage(draft->style)};
            qCDebug(lcRendering) << "typed text remade";
        } catch (const std::runtime_error &error) {
            qCWarning(lcRendering) << "typed text could not be drawn:" << error.what();
            m_draftTextCache.reset();
            return std::nullopt;
        }
    }
    return DraftText{m_draftTextCache->image, m_inlineTextEditor->shownTransform()};
}

// In its layer's place, effects redone as it is typed.
void CanvasView::drawTypedText(const ImageLayer &layer, const LayerRenderer::Options &options, const Center &center, QPainter &target)
{
    const std::optional<DraftText> text = draftText();
    if (!text)
        return;
    const LayerEffects effects = layer.effects.value_or(LayerEffects()).visible();
    if (!effects.isEmpty() && effects.isValid()) {
        // Redone only when the pixels, their place or effects change.
        if (!m_draftEffects || m_draftEffects->image.cacheKey() != text->image.cacheKey() || m_draftEffects->effects != effects
            || m_draftEffects->transform != text->transform) {
            std::optional<QImage> mask;
            if (layer.mask && layer.mask->placement)
                mask = layer.mask->clipImage(*layer.mask->placement, text->transform, text->image.width(), text->image.height(), 2048);
            else if (layer.mask)
                mask = layer.mask->enabledImage();
            const std::optional<EffectsPreviewCache::Result> built = m_session.effectsPreviews.renderNow(text->image, mask, effects);
            m_draftEffects = built ? std::optional(DraftEffects{text->image, effects, text->transform, built->image, built->inset}) : std::nullopt;
            m_draftEffectsSource = DraftEffectsSource{layer.id, m_session.textDraft().value().style};
        }
        // The last effects stand in until a change is redone.
        if (m_draftEffects) {
            const LayerTransform grown = LayerEffectsRenderer::placed(text->transform, m_draftEffects->rendered, m_draftEffects->inset);
            LayerRenderer::draw(m_draftEffects->rendered, grown, center(grown.center()), target,
                                {.scale = options.scale, .opacity = options.opacity, .blendMode = options.blendMode, .clip = options.clip});
            return;
        }
    }
    LayerRenderer::draw(text->image, text->transform, center(text->transform.center()), target,
                        {.scale = options.scale, .opacity = options.opacity, .blendMode = options.blendMode, .clip = options.clip});
}

// New text: above the active layer, else on top.
void CanvasView::drawNewText(bool &drawn, double scale, const Center &center, QPainter &target, const QImage &clip)
{
    // Asked without a draft too: that lets the cache go.
    const std::optional<TextDraft> &draft = m_session.textDraft();
    if (drawn || (draft && draft->layerID))
        return;
    const std::optional<DraftText> text = draftText();
    if (!text)
        return;
    drawn = true;
    LayerRenderer::draw(text->image, text->transform, center(text->transform.center()), target, {.scale = scale, .clip = clip});
}

// Once committed, the typed text's effects stand in: nothing blinks.
void CanvasView::handOnDraftEffects(const CanvasDocument &document)
{
    if (m_session.textDraft() || !m_draftEffects || !m_draftEffectsSource)
        return;
    const int index = indexOf(document.layers, m_draftEffectsSource->layerID);
    if (index >= 0 && document.layers[size_t(index)].liveText() && document.layers[size_t(index)].liveText()->style == m_draftEffectsSource->style)
        m_session.effectsPreviews.seed(m_draftEffectsSource->layerID, m_draftEffects->rendered,
                                       LayerEffectsRenderer::placed(m_draftEffects->transform, m_draftEffects->rendered, m_draftEffects->inset));
    m_draftEffects.reset();
    m_draftEffectsSource.reset();
}
