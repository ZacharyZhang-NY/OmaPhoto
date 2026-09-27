// Swift's CameraRaw.swift session extension; in EditorSession's body.
public:
    // The panel's state, which Swift's panel writes on the edit.
    void setCameraRawPanel(const CameraRawPanel &panel);
    // The white-balance eyedropper: the clicked pixel turns neutral.
    void sampleCameraRawWhiteBalance(QPointF point);
    // White Balance Auto: the mode now, the sliders later.
    void applyCameraRawAutoWhiteBalance(std::function<void()> done = {});
    // The defringe eyedropper centres a range on the fringe.
    void sampleCameraRawDefringe(QPointF point);
    // The adjusted preview's RGB under the pointer.
    void updateCameraRawReadout(QPointF point);
    void beginCameraRawDrag(QPointF point);
    void dragCameraRaw(QPointF point);
    void sampleCameraRawPointColor(QPointF point);
    void beginCameraRawGeometryGuide(QPointF point);
    void continueCameraRawGeometryGuide(QPointF point);
    void commitCameraRawGeometryGuide();

private:
    std::optional<QPointF> cameraRawNormalizedPoint(QPointF point) const;
    struct CameraRawSample {
        double tone;
        double hue;
        double saturation;
        double luminance;
    };
    std::optional<CameraRawSample> cameraRawSample(QPointF point) const;
