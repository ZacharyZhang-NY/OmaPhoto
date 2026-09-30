# OmaPhoto 1.3.3 acceptance

The MVP acceptance table for OmaPhoto 1.3.3, the Linux port of Compositor 1.3.3. Each row is a feature from the README.

The acceptance run drives the app's own window, the way a person uses it. It chooses menu entries, drags on the canvas, types into sheets and answers the real file panels. Then it checks concrete results: pixels, layers, sizes and files on disk. The run is three test programs: `AcceptanceTests`, `AcceptanceColorTests` and `AcceptanceProjectTests`. They take the screenshots below when `ACCEPTANCE_SHOTS` names a folder.

```sh
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$PWD:$PWD" -w "$PWD" omaphoto-dev \
  sh -c 'cmake -S . -B build && cmake --build build --target AcceptanceTests AcceptanceColorTests AcceptanceProjectTests &&
         for t in AcceptanceTests AcceptanceColorTests AcceptanceProjectTests; do ACCEPTANCE_SHOTS=$PWD/docs/acceptance build/$t || exit 1; done'
```

The screenshots come from Qt's offscreen platform, so floating panels appear without the window manager's title bars. The run uses the built-in dark theme, except for the last row.

## Table

| Feature | What the run does and checks | Screenshot | Suites that hold the details |
| --- | --- | --- | --- |
| Layers, folders, masks, clipping masks, blend modes, opacity | Imports a photo and paints a layer. Clips it to the photo and sets Multiply at 60%. Masks the photo and groups both in a folder. Undo takes a step back. | [01](acceptance/01-layers.png) | `GroupTests`, `LayerMaskTests`, `LiveMaskTests`, `LayerAppearanceTests`, `LayerRendererTests`, `HistoryTests` |
| Selections | Drags a marquee to within 2 px. The wand takes the flat ground. Select › Subject finds the red disc with the U²-Net model. | [02](acceptance/02-selections.png) | `SelectionTests`, `MagicWandTests`, `SelectSubjectTests`, `SelectionEditTests`, `ObjectSelectionTests`, `SelectionFeatherTests` |
| Painting tools | Drags a gradient that fades to clear, a rectangle shape and a soft white brush stroke. Types "Hello OmaPhoto" on the canvas and colours "Hello" red. That leaves one colour run, committed with Ctrl+Return. | [03](acceptance/03-painting.png) | `BrushTests`, `GradientTests`, `ShapeToolTests`, `TypeToolTests`, `TextColorRunsTests`, `SpotHealingTests`, `CloneStampTests`, `SmudgeLiquifyTests` |
| Move, transform, distort; rulers, guides, grid | Drags the layer with the Move tool. Transform Layer halves it, and Return applies. Rulers, a guide and the grid are shown. | [04](acceptance/04-transform-guides-grid.png) | `TransformSessionTests`, `DistortTests`, `GuideTests`, `FloatingSelectionTests` |
| Adjustments as edits and as layers | Hue/Saturation turns the red disc green. An Invert layer inverts the view and leaves the pixels alone. A Curves layer opens its editor. | [05](acceptance/05-adjustments.png) | `HueSaturationTests`, `LevelsTests`, `CurvesControlsTests`, `AdjustmentLayerTests`, `AdjustmentEditorTests`, `ImageAdjustmentTests` |
| Camera Raw Filter, docked | Opens in the dock beside the canvas. One stop of exposure previews, commits and brightens the ground. | [06](acceptance/06-camera-raw.png) | `CameraRawTests`, `CameraRawSessionTests`, `CameraRawPanelTests`, `CameraRawCanvasTests` |
| Layer effects | A 12 px red stroke rings a shape outside its edge, and a drop shadow's sheet shows. The shape keeps its fill. | [07](acceptance/07-effects.png) | `LayerEffectsTests`, `LayerEffectsKernelTests`, `EffectsPanelTests`, `EffectsCanvasTests`, `InnerGlowTests` |
| Filters | Dither leaves only black and white dots. Remove Background masks the photo around its subject: the centre stays opaque and a corner turns clear. | [08](acceptance/08-filters-dither.png), [09](acceptance/09-remove-background.png) | `DitherTests`, `FilterTests`, `FinishingFilterTests`, `SubjectRemovalTests`, `RemoveBackgroundTests` |
| Crop, trim, canvas size, image size; PNG and JPEG export | Crops to 1000 × 600. Canvas Size widens it to 1400. Trim finds the photo's own 1200 again. Image Size halves it. Exports a PNG through the save panel and a JPEG through its sheet and panel, then reads both back at 600 × 300. | [10](acceptance/10-canvas-size.png), [11](acceptance/11-export-jpeg.png) | `CropTests`, `ImageTrimTests`, `ProjectResizeTests`, `ProjectExportTests`, `JPEGExportTests` |
| Imports | Takes JPEG, PNG, TIFF, HEIC, SVG, a layered Photoshop file (it arrives as a folder) and a camera RAW file, which asks how to develop it. That makes eight layers with no import error. | [12](acceptance/12-raw-develop.png), [13](acceptance/13-imports.png) | `ImageImportTests`, `PSDImportTests`, `RawImporterTests`, `RawDevelopTests`, `SVGImportTests` |
| Open Recent; Keyboard Shortcuts window | Saves a project, and Open Recent lists it. The shortcuts window opens over the editor. | [15](acceptance/15-shortcuts.png) | `OpenRecentTests`, `RecentProjectsCoreTests`, `KeyboardShortcutsTests`, `KeyboardShortcutsSheetTests` |
| Scrub a number by its label | Dragging the Opacity title 40 points left lowers the layer's opacity. | [14](acceptance/14-scrub.png) | `NumericScrubTests`, `ScrubbableLabelsTests`, `ScrubbableSheetsTests` |
| Keep working while a project saves | Another layer is added while a save runs. The file holds the state the save began with. | — | `ProjectSaveWhileEditingTests` |
| Reload a project changed on disk | With unsaved edits open, another program renames a layer on disk. The app's alert asks, Revert is chosen, and the saved state shows with the new name. | [15](acceptance/15-shortcuts.png) | `ExternalChangeTests` |
| Follows the Omarchy theme | Starts in the built-in dark. A Catppuccin Latte `colors.toml` written while the app runs retints the window. | [16](acceptance/16-omarchy-light.png) | `OmarchyThemeTests` |
| Builds and tests on each system | The full suite passed on Ubuntu 24.04 (Qt 6.4.2), Ubuntu 26.04, Arch (Qt 6.11), Fedora 43 and 44, and NixOS. Warnings count as errors. | — | `scripts/distro-check.sh` |
| Packages and install scripts | Each install script ran from GitHub in a fresh container. On Ubuntu 24.04 and 26.04, Fedora 43 and 44 and Arch, it checked its package against `SHA256SUMS`, installed it and started 1.3.3. On NixOS, `nixos.sh` resolved `v1.3.3`, built its flake and started 1.3.3. Debian 13 was refused, as intended. | — | `DesktopIntegrationTests`, `.github/workflows/release.yml` |

## Known differences from Compositor

`AGENTS.md` lists each place where the port differs from the Swift app, and why. The main ones:

- Remove Background and Select Subject use U²-Net on ONNX Runtime, not Apple's Vision.
- Rendering is CPU only.
- There is no updater.
- A `.comp` project is a folder.
- A Wayland compositor places windows itself.
