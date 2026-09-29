// Swift's HueSaturation extension.
public:
    bool canAdjustColors() const;
    bool canVignette() const;
    void beginHueSaturation();
    // Previews coalesce: the newest waits for the running one.
    void updateHueSaturation(const HueSaturationSettings &settings, bool preview);
    // Off the UI thread; `done` follows the step or refusal.
    void commitHueSaturation(std::function<void()> done = {});
    // The hue under a document point; near-grays have none.
    std::optional<double> sampledHue(QPointF point) const;
    // The eyedroppers: re-centre, widen or narrow the band.
    void sampleHueRange(QPointF point);
    // Targeted adjustment: the range owning the sampled colour.
    bool beginHueTargeting(QPointF point);
    // A unit every two view points, from the drag's start.
    void dragHueTargeting(double viewDelta, bool adjustsHue);
    void endHueTargeting();
    void cancelHueSaturation();
    // The sheet's writes, as Swift's sheet sets them.
    void setHueSampleMode(std::optional<HueSampleMode> mode);
    void setHueTargeting(bool targeting);

private:
    bool canAdjust(bool allowingEmpty) const;
    void renderPendingHuePreview();
    void finishHueSaturationPreview();
    void finishHueSaturationCommit();
