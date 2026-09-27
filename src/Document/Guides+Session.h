// Swift's Guides extension, and the View menu's guide state.
public:
    static constexpr double guideHitDistance = 5;
    bool showsGrid() const { return m_showsGrid; }
    void setShowsGrid(bool shows);
    bool showsGuides() const { return m_showsGuides; }
    void setShowsGuides(bool shows);
    bool showsRulers() const { return m_showsRulers; }
    void setShowsRulers(bool shows);
    // View > Snap To's master switch, beside 1.1.6's Snap.
    bool snapEnabled() const { return m_snapEnabled; }
    void setSnapEnabled(bool enabled);
    bool snapToGuides() const { return m_snapToGuides; }
    void setSnapToGuides(bool snaps);
    bool snapToGrid() const { return m_snapToGrid; }
    void setSnapToGrid(bool snaps);
    bool snapToLayers() const { return m_snapToLayers; }
    void setSnapToLayers(bool snaps);
    bool snapToDocumentBounds() const { return m_snapToDocumentBounds; }
    void setSnapToDocumentBounds(bool snaps);
    bool locksGuides() const { return m_locksGuides; }
    void setLocksGuides(bool locks);
    const std::optional<GuideDrag> &guideDrag() const { return m_guideDrag; }
    bool canClearGuides() const;
    bool canEditGuides() const;
    // The guides as shown, a drag under way included.
    std::vector<CanvasGuide> displayedGuides() const;
    std::optional<CanvasGuide> hitGuide(QPointF viewPoint, double tolerance = guideHitDistance) const;
    void beginGuideCreation(CanvasGuide::Axis axis, double position);
    void beginGuideMove(const CanvasGuide &guide);
    void moveGuideDrag(double position);
    // Dropped on a ruler, the guide goes.
    void finishGuideDrag(bool removing);
    void cancelGuideDrag();
    void clearGuides();
    void addGuide(const CanvasGuide &guide);
    // What a move or crop snaps to, per Snap To.
    SnapGuides alignmentSnapTargets(const QSet<QUuid> &moving, bool includeCenters) const;
    double snappedGuidePosition(double position, CanvasGuide::Axis axis, std::optional<QUuid> excluding) const;
