# OmaPhoto 1.2.3

OmaPhoto 1.2.3 catches up with Compositor 1.2.3. It follows 1.0.1.

## New tools and commands

- **Camera Raw Filter.** A panel docked beside the canvas. It has a histogram and vectorscope with clipping warnings, and Light, Color and Effects. It adds Curve (parametric and point), Color Mixer (HSL, colour and point colour), Color Grading wheels, Detail, Optics, Geometry (Upright, guides, projection) and Calibration. Eyedroppers and targeted drags work on the canvas.
- **Camera RAW import** through LibRaw, with a develop sheet.
- **Photoshop PSD import** with its layers. What cannot convert is listed in a report.
- **Image > Trim…** cuts away transparent edges, or edges matching the top-left or bottom-right colour.
- **Magic tool Object mode**, using the U²-Net model.
- **Select Subject** and feathered selections. Expand and Contract now open amount dialogs.
- **Rulers, a layout grid and guides**, with Snap To settings in the View menu.
- **A Keyboard Shortcuts window** for menu, canvas and text-editing shortcuts.

## Layers and effects

- **Adjustment layers:** Black & White, Color Balance, Invert, Gaussian Blur, Motion Blur and Add Noise join the others. Black & White and Color Balance also work as direct Image adjustments.
- **Layer effects:** Outer Glow and Inner Glow.
- **Blend modes:** eleven new ones, Soft Light among them, arranged in Photoshop's groups.
- Folders keep their contents when duplicated, and folders have their own opacity.

## Brushes, text and canvas

- **Brush Smoothing.**
- Faster text editing. The font menu keeps a fixed width and fills only when opened.
- Middle-button panning.
- Auto Select, guides, the grid and snapping keep their settings between sessions.
- Auto Select picks the layer in front.
- A new app icon.

## Projects

- OmaPhoto 1.2.3 saves project format 9 and opens formats 1 to 9, as Compositor 1.2.3 does. Projects saved here cannot be reopened in OmaPhoto 1.0.1 or Compositor 1.2.2, even without the new adjustment layers.

## Fixes

- Fixed an export crash caused by Qt hash allocations when memory was low.
- Export failure tests are steady under load.
- Camera Raw's point-colour preview works, where Compositor's does not. Outer Glow and Inner Glow keep their size in reduced previews.

## Packages

DEB (Ubuntu 24.04 and 26.04), RPM (Fedora), Arch (PKGBUILD) and Nix (flake) are built from the same install.
