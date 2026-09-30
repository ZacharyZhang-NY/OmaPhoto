# OmaPhoto 1.3.3

OmaPhoto 1.3.3 catches up with Compositor 1.3.3. It follows 1.2.3.

## New tools and commands

- **Filter › Dither.** Error diffusion (Atkinson, Floyd–Steinberg), ordered Bayer, halftone dots, lines and diamonds, patterns and ASCII. Chunky pixels, square or round. Black and white, two colours from the picker, or the image's own.
- **Finishing filters:** Vignette, Bloom / Glow and Tonal Contrast. Vignette also paints across an empty layer.
- **File › Open Recent**, with Clear Menu.
- **Reload on change.** A project changed on disk by another program reloads. With unsaved edits it asks first: revert, or keep what you have.
- **Photoshop:** Large Documents (PSB), files with only a merged image, oversized layers cropped to the canvas, and simple text layers as editable text.
- **SVG import.**
- **Whole layers on the clipboard**, within a project and between projects. Ctrl+J duplicates every selected layer, folders included.
- **A layer context menu** in the Layers panel.
- **Scrubbing.** Drag a number's title to change it, in the tool bars, the layer opacity, the sheets and the panels.
- **Crop ratios** 3:4 and 9:16, and a crop that starts at the selection.

## Text

- Colour only the selected letters of the text being edited. The swatch follows the caret, and the selection stays see-through under the picker.
- Text being typed shows as the pixels it will commit to.
- A click places text on its first baseline. The Move tool's double click opens live text.
- Closing or quitting while text is edited applies it first.
- Filters, adjustments and Invert wait until text editing ends. Closing the colour picker gives open text the keys back.

## Layers, masks and panels

- Masks can be painted anywhere on the canvas. A moved, unlinked mask stays in place under layer effects. Mask thumbnails show a white or black ground, as the canvas treats it.
- Layer effects stay on screen while text is edited, masks are painted and steps are undone.
- Coloured slider tracks with double-click reset in Black & White, Color Balance and Hue/Saturation. Filter sliders line up under the widest title.
- Camera Raw's Color Grading sits under Color and starts open.
- Image Size keeps print sizes through an invalid resolution.
- Stepped keyboard zoom and a steady tab strip.
- Marching ants stay smooth when zoomed out. The document's pixel budget follows the machine's memory.

## Projects

- Editing goes on while a project saves. Saves, reloads and opens wait their turn behind one writer.
- OmaPhoto 1.3.3 saves project format 10 and opens formats 1 to 10, as Compositor 1.3.3 does. Projects saved here cannot be reopened in OmaPhoto 1.2.3 or Compositor 1.3.1.
- `docs/writing-comp-files.md` guides AI agents that write projects.

## Fixes

- A sheet or Camera Raw group closed while one of its fields held the keys no longer writes into freed memory.
- Export failure tests are steady: a project snapshot no longer holds two layer lists while it grows.
- Overlapping writes, and a quit before a write finished, can no longer lose a save.

## Packages

DEB (Ubuntu 24.04 and 26.04), RPM (Fedora), Arch (PKGBUILD) and Nix (flake) are built from the same install.
