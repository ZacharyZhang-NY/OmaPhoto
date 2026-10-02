# OmaPhoto 1.4.5 acceptance

The acceptance table for OmaPhoto 1.4.5, the Linux port of Compositor 1.4.5. Each row is a feature from the README. The rows under "New in 1.4.5" cover what changed since 1.3.3. A "pending" result waits for the 1.4.5 run.

The acceptance run drives the app's own window, the way a person uses it. It chooses menu entries, drags on the canvas, types into sheets and answers the real file panels. Then it checks concrete results: pixels, layers, sizes and files on disk. The run is three test programs: `AcceptanceTests`, `AcceptanceColorTests` and `AcceptanceProjectTests`. They take the screenshots below when `ACCEPTANCE_SHOTS` names a folder.

```sh
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$PWD:$PWD" -w "$PWD" omaphoto-dev \
  sh -c 'cmake -S . -B build && cmake --build build --target AcceptanceTests AcceptanceColorTests AcceptanceProjectTests &&
         for t in AcceptanceTests AcceptanceColorTests AcceptanceProjectTests; do ACCEPTANCE_SHOTS=$PWD/docs/acceptance build/$t || exit 1; done'
```

The screenshots come from Qt's offscreen platform, so floating panels appear without the window manager's title bars. The run uses the built-in dark theme, except for the last row.

## Table

| Feature | What the run does and checks | Screenshot | Suites that hold the details | Result |
| --- | --- | --- | --- | --- |
| Layers, folders, masks, clipping masks, blend modes, opacity | Imports a photo and paints a layer. Clips it to the photo and sets Multiply at 60%. Masks the photo and groups both in a folder. Undo takes a step back. | [01](acceptance/01-layers.png) | `GroupTests`, `LayerMaskTests`, `LiveMaskTests`, `LayerAppearanceTests`, `LayerRendererTests`, `HistoryTests` | pending |
| Selections | Drags a marquee to within 2 px. The wand takes the flat ground. Select › Subject finds the red disc with the U²-Net model. | [02](acceptance/02-selections.png) | `SelectionTests`, `MagicWandTests`, `SelectSubjectTests`, `SelectionEditTests`, `ObjectSelectionTests`, `SelectionFeatherTests` | pending |
| Painting tools | Drags a gradient that fades to clear, a rectangle shape and a soft white brush stroke. Types "Hello OmaPhoto" on the canvas and colours "Hello" red. That leaves one colour run, committed with Ctrl+Return. | [03](acceptance/03-painting.png) | `BrushTests`, `GradientTests`, `ShapeToolTests`, `TypeToolTests`, `TextColorRunsTests`, `SpotHealingTests`, `CloneStampTests`, `SmudgeLiquifyTests` | pending |
| Move, transform, distort; rulers, guides, grid | Drags the layer with the Move tool. Transform Layer halves it, and Return applies. Rulers, a guide and the grid are shown. | [04](acceptance/04-transform-guides-grid.png) | `TransformSessionTests`, `DistortTests`, `GuideTests`, `FloatingSelectionTests` | pending |
| Adjustments as edits and as layers | Hue/Saturation turns the red disc green. An Invert layer inverts the view and leaves the pixels alone. A Curves layer opens its editor. | [05](acceptance/05-adjustments.png) | `HueSaturationTests`, `LevelsTests`, `CurvesControlsTests`, `AdjustmentLayerTests`, `AdjustmentEditorTests`, `ImageAdjustmentTests` | pending |
| Camera Raw Filter, docked | Opens in the dock beside the canvas. One stop of exposure previews, commits and brightens the ground. | [06](acceptance/06-camera-raw.png) | `CameraRawTests`, `CameraRawSessionTests`, `CameraRawPanelTests`, `CameraRawCanvasTests` | pending |
| Layer effects | A 12 px red stroke rings a shape outside its edge, and a drop shadow's sheet shows. The shape keeps its fill. | [07](acceptance/07-effects.png) | `LayerEffectsTests`, `LayerEffectsKernelTests`, `EffectsPanelTests`, `EffectsCanvasTests`, `InnerGlowTests` | pending |
| Filters | Dither leaves only black and white dots. Remove Background masks the photo around its subject: the centre stays opaque and a corner turns clear. | [08](acceptance/08-filters-dither.png), [09](acceptance/09-remove-background.png) | `DitherTests`, `FilterTests`, `FinishingFilterTests`, `SubjectRemovalTests`, `RemoveBackgroundTests` | pending |
| Crop, trim, canvas size, image size; PNG and JPEG export | Crops to 1000 × 600. Canvas Size widens it to 1400. Trim finds the photo's own 1200 again. Image Size halves it. Exports a PNG through the save panel and a JPEG through its sheet and panel, then reads both back at 600 × 300. | [10](acceptance/10-canvas-size.png), [11](acceptance/11-export-jpeg.png) | `CropTests`, `ImageTrimTests`, `ProjectResizeTests`, `ProjectExportTests`, `JPEGExportTests` | pending |
| Imports | Takes JPEG, PNG, TIFF, HEIC, SVG, a layered Photoshop file (it arrives as a folder) and a camera RAW file, which asks how to develop it. That makes eight layers with no import error. | [12](acceptance/12-raw-develop.png), [13](acceptance/13-imports.png) | `ImageImportTests`, `PSDImportTests`, `RawImporterTests`, `RawDevelopTests`, `SVGImportTests` | pending |
| Open Recent; Keyboard Shortcuts window | Saves a project, and Open Recent lists it. The shortcuts window opens over the editor. | [15](acceptance/15-shortcuts.png) | `OpenRecentTests`, `RecentProjectsCoreTests`, `KeyboardShortcutsTests`, `KeyboardShortcutsSheetTests` | pending |
| Scrub a number by its label | Dragging the Opacity title 40 points left lowers the layer's opacity. | [14](acceptance/14-scrub.png) | `NumericScrubTests`, `ScrubbableLabelsTests`, `ScrubbableSheetsTests` | pending |
| Keep working while a project saves | Another layer is added while a save runs. The file holds the state the save began with. | — | `ProjectSaveWhileEditingTests` | pending |
| Reload a project changed on disk | With unsaved edits open, another program renames a layer on disk. The app's alert asks, Revert is chosen, and the saved state shows with the new name. | [15](acceptance/15-shortcuts.png) | `ExternalChangeTests` | pending |
| Follows the Omarchy theme | Starts in the built-in dark. A Catppuccin Latte `colors.toml` written while the app runs retints the window. | [16](acceptance/16-omarchy-light.png) | `OmarchyThemeTests` | pending |
| Builds and tests on each system | The full suite runs on Ubuntu 24.04 (Qt 6.4.2), Ubuntu 26.04, Arch, Fedora 43 and 44, and NixOS. Warnings count as errors. | — | `scripts/distro-check.sh` | pending |
| Packages and install scripts | Each install script runs from GitHub in a fresh container. On Ubuntu 24.04 and 26.04, Fedora 43 and 44 and Arch, it checks its package against `SHA256SUMS`, installs it and starts 1.4.5. On NixOS, `nixos.sh` resolves `v1.4.5`, builds its flake and starts 1.4.5. Debian 13 is refused, as intended. | — | `DesktopIntegrationTests`, `.github/workflows/release.yml` | pending |

## New in 1.4.5

| Feature | What the run checks | Screenshot | Suites that hold the details | Result |
| --- | --- | --- | --- | --- |
| Select › Color Range | Samples the red disc; the selection covers the disc and leaves the ground. | — | `ColorRangeTests`, `ColorRangeSheetTests`, `ColorRangeCanvasTests` | pending |
| View › Grid Settings | Sets spacing, subdivisions, colour and style; the grid redraws; Restore Defaults puts them back. | — | `GridSettingsSheetTests`, `GuideGridTests` | pending |
| Snapping for selections and shapes | A marquee and a rectangle shape drawn near a guide land on it; with Ctrl they do not. | — | `SnapDrawingCanvasTests`, `TransformSnapTests` | pending |
| New Canvas presets | Chooses 1080p and Instagram Story from the ⋮ menu; the fields and the check follow. | — | `NewCanvasSheetTests` | pending |
| Quick Look preview | A saved project holds `QuickLook/Preview.jpg`, 1024 px on the long side, and reopens unchanged. | — | `QuickLookPreviewTests` | pending |
| Ungroup Layers | Ctrl+Shift+G on a folder puts its layers in its place; Undo brings the folder back. | — | `GroupSessionTests`, `LayerContextMenuTests` | pending |
| Project tabs reorder and overflow | Drags a tab past its neighbour; with many tabs, the "more tabs" menu switches to a hidden one. | — | `ProjectTabDragTests`, `ProjectTabLayoutTests` | pending |
| Fonts on selected letters and previews | Changes the face of one word; hovering a face in the menu previews it; Ctrl+Z takes back typing. | — | `TextFontRunsTests`, `TypeFontPreviewTests`, `InlineTextDrawTests` | pending |
| Painting at the layer's resolution | Paints on a scaled-down layer; the stroke is smooth at the layer's own pixels. A hidden layer says why it cannot paint. | — | `NativeResolutionPaintTests`, `PaintRefusalTests` | pending |
| Liquify, Smudge and Blur | Liquify keeps edges sharp; Smudge leaves no copies; the Blur brush's Radius applies. | — | `SmudgeLiquifyTests`, `BlurToolTests` | pending |
| Masks as Photoshop's | The mask button reveals the selection, Alt hides it; Alt-click on a mask thumbnail shows the mask alone. | — | `MaskAloneTests`, `SelectionEditTests` | pending |
| Move tool keys and values | Held Ctrl flips Auto Select; a typed width applies as one step; a resize handle snaps. | — | `TransformInspectorTests`, `TransformSnapTests` | pending |
| Blending and a sharper canvas | Soft Light, clipping stacks and adjustment layers draw as the export does. | — | `SharpCanvasTests`, `BlendModeTests`, `ClippingStackTests` | pending |
| Camera Raw curves | Drags a region of the parametric curve and a point on a channel's curve; the image follows. | — | `CameraRawCurveMixerTests`, `CameraRawTests` | pending |
| Dither ASCII and Scanlines | ASCII draws letters at the chosen size; Scanlines (CRT) applies. | — | `DitherTests`, `ScanlinesTests` | pending |
| Export JPEG preview | Zooms the preview to 100% and back to Fit; the View menu's zoom keys work on it. | — | `JPEGPreviewTests`, `JPEGExportSheetTests` | pending |
| Quit mid-edit | Quits with a gradient pending and a dialog open; the app asks once and closes. | — | `QuitPendingEditsTests`, `WindowTeardownTests` | pending |

## Known differences from Compositor

`AGENTS.md` lists each place where the port differs from the Swift app, and why. The main ones:

- Remove Background and Select Subject use U²-Net on ONNX Runtime, not Apple's Vision.
- Rendering is CPU only.
- There is no updater.
- A `.comp` project is a folder.
- A Wayland compositor places windows itself.
