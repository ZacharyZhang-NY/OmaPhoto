// Swift's LayerEffects extension: effects kept with their layer.
public:
    bool canEditEffects() const;
    LayerEffects activeEffects() const;
    // The effects of the layer whose panel is open.
    LayerEffects editingEffects() const;
    // The chosen effect, if active and still on its layer.
    std::optional<LayerEffectSelection> selectedEffect() const;
    // Adds the effect with its defaults, then opens its panel.
    void addEffect(LayerEffectKind kind);
    void selectEffect(LayerEffectKind kind, QUuid id, bool editing = false);
    // Cancel puts back this panel's effect alone.
    void finishEffectsEditing(bool commit);
    // One step, on the active layer unless another is named.
    void setEffects(const LayerEffects &effects, std::optional<QUuid> id = std::nullopt, const QString &name = QStringLiteral("Layer Effects"));
    // Panel edits stay with the layer that opened the panel.
    void changeEffects(const std::function<void(LayerEffects &)> &change);
    bool canCopyEffect(LayerEffectKind kind, QUuid source, QUuid target) const;
    void copyEffect(LayerEffectKind kind, QUuid source, QUuid target);
    void toggleEffect(LayerEffectKind kind, QUuid id);
    void removeSelectedEffect();
    // Swift's `effectSelection = nil`, before any guard; announced.
    void dropEffectSelection();

private:
    // An open effect colour picker goes with its panel.
    void closeEffectColorPicker(bool commit);
