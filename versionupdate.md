# OmaPhoto 1.4.5

OmaPhoto 1.4.5 catches up with Compositor 1.4.5. It follows 1.3.3.

## New tools and commands

- **Select › Color Range.** Pick colours on the canvas and widen or narrow the match. The selection follows them.
- **View › Grid Settings.** Set the grid's spacing, subdivisions, colour, style and opacity. Restore Defaults puts them back. The settings last across launches.
- **Snapping for selections and shapes.** The Marquee, the Shape tool and a moved selection snap to what View › Snap To names. Hold Ctrl to draw freely.
- **New Canvas presets.** A ⋮ button beside the title offers common sizes: 4K, 1440p, 1080p, Apple screens and social formats. The size the fields hold is checked.
- **Layer › Ungroup Layers** (Ctrl+Shift+G), also in a folder's right-click menu. The folder's layers take its place, in order.
- **Project tabs drag to reorder.** Tabs that do not fit gather under a "3 more tabs" pill. Its menu lists them and switches to the one chosen. The tab in front always shows.

## Painting and editing

- A brush paints at the layer's own resolution. Clone Stamp and Blur sample there too. A blank layer paints at the document's resolution.
- When a brush cannot paint, the status line says why: a hidden layer, a folder, a mask turned off, an empty selection and so on.
- Liquify keeps pixels sharp. Smudge leaves no ghost copies. The Blur brush has its own Radius. Blur, Smudge and Liquify work on big canvases.
- Shift keeps moved pixels on a straight line.
- Select All, then Inverse, deselects.
- Ctrl+A selects the canvas from the Layers panel too.
- In the Move tool, Ctrl flips Auto Select and Shift the aspect lock while held, and the bar shows it. Values typed in the bar apply at once, as one undo step. Resize handles snap.
- Cancel and Apply show only when an edit waits for them. A scaled layer shows its scale.

## Text

- A font change applies to the selected letters. A mixed selection shows (Multiple).
- The Type bar's font menu draws each name in its own face. Hovering a face previews it on the text.
- Ctrl+Z while typing takes back what was typed.

## Masks and layers

- Masks work as in Photoshop. The mask button reveals the selection, and Alt-click hides it. Alt-click on a mask thumbnail shows the mask alone. Alt over a mask thumbnail shows a pointer with an eye.
- The canvas is sharper with adjustment layers. An upright layer at 1:1 draws pixel for pixel.
- Clipping stacks blend in their base's mode everywhere: on the canvas, in export and in Copy Merged.
- Soft Light follows Photoshop's formula. Soft, partly clear layers blend alike in Soft Light, Overlay, Multiply and Color Dodge.
- Positive Saturation matches Photoshop. Levels and Hue/Saturation use every core.
- Grain and Add Noise stay put while you pan and zoom.
- Photoshop files keep their adjustments and masks better.

## Filters and adjustments

- Camera Raw's curves work as Photoshop's: a smooth parametric curve and point curves for each channel. Drag a region of the graph to lift or lower it. Refine Saturation has its new meaning.
- Dither's ASCII draws readable text, with its own text size. Dither › Scanlines (CRT) runs on every core.
- A blur's preview stays while the layer grows.

## Export and sheets

- Export JPEG has a zoomable preview at full size, with Fit and zoom steps. The View menu's zoom keys work on it.
- Export JPEG and Canvas Size use the app's own colour picker.
- An import error has one OK.

## Projects

- Every save writes `QuickLook/Preview.jpg` into the project folder, as Compositor does. On a Mac, Finder's Space-bar preview shows it. Loading ignores it.
- OmaPhoto 1.4.5 saves project format 11 and opens formats 1 to 11, as Compositor 1.4.5 does. Projects saved here cannot be reopened in OmaPhoto 1.3.3.

## Fixes

- Quit works in the middle of a gradient or with a dialog open. Pending edits are applied or cancelled first.
- Two crashes when a window closed are fixed.
- On NVIDIA with Hyprland at a fractional scale, OmaPhoto's own cursors can show garbled. The README names the workaround: software cursors.

## Packages

DEB (Ubuntu 24.04 and 26.04), RPM (Fedora 43 and 44), Arch (PKGBUILD) and Nix (flake) are built from the same install.
