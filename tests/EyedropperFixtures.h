#pragma once
#include "InlineTextFixtures.h"
#include "UI/ColorPickerSheet.h"

// Shared by the Eyedropper's tests and picking's.
namespace {
// Red left, blue right, filling the view.
struct Picking : Canvas {
    Picking()
    {
        QImage image(400, 300, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::blue);
        for (int y = 0; y < 300; ++y) {
            for (int x = 0; x < 200; ++x)
                image.setPixelColor(x, y, Qt::red);
        }
        session.insert(ImportedImage(image, image, QStringLiteral("Split")));
        tool(NavigationTool::eyedropper);
    }
    void tool(NavigationTool tool)
    {
        session.selectTool(tool);
        canvas->synchronizeDisplay();
    }
    QString foreground() const { return session.foregroundColor().hex(); }
    const SampleRingOverlay &ring() const { return canvas->sampleRing(); }
    bool picks() const { return shows(CanvasView::eyedropperCursor(canvas->devicePixelRatio())); }
};

inline QWidget *pickerPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == ColorPickerPanelController::identifier() && widget->isVisible())
            return widget;
    }
    return nullptr;
}
}
