# Writing OmaPhoto projects (for AI agents and scripts)

An OmaPhoto project (`.comp`) is a folder of PNG layer images plus a `manifest.json`: the same format as [Compositor](https://github.com/robbietilton/Compositor)'s, so a project moves between the two apps. Anything that can write files can build or edit one, and OmaPhoto updates the open canvas as the files change. No plugin or API is involved.

## Try it

1. Open a project in OmaPhoto 1.3 or later (save a new canvas somewhere, e.g. `~/Pictures/demo.comp`), and keep it open.
2. Ask an AI agent that can edit files on your computer (Claude Code, Codex and the like):

   > Read docs/writing-comp-files.md in github.com/ZacharyZhang-NY/OmaPhoto, then design a moody night scene in ~/Pictures/demo.comp. Work in steps, one or two layers at a time.

3. Watch the canvas. Each time the agent writes the project, OmaPhoto reloads it, usually within half a second.

What you get are ordinary layers: select them, change their opacity or blend mode, paint on their masks, save.

## The project folder

```
Example.comp/
├── manifest.json
└── images/
    ├── 6F1D3C2A-0B7E-4E8A-9C4D-2A1B3C4D5E6F.png        a layer's pixels
    └── 6F1D3C2A-0B7E-4E8A-9C4D-2A1B3C4D5E6F.mask.png   its mask (optional)
```

A minimal manifest with one full-canvas image layer:

```json
{
  "format": "com.compositor.project",
  "version": 10,
  "colorSpace": "sRGB",
  "documentID": "0C5E7A91-3B2D-4F6A-8E1C-9D0B7A6F5E4D",
  "width": 1920,
  "height": 1080,
  "resolution": 72,
  "activeLayerID": "6F1D3C2A-0B7E-4E8A-9C4D-2A1B3C4D5E6F",
  "layers": [
    {
      "id": "6F1D3C2A-0B7E-4E8A-9C4D-2A1B3C4D5E6F",
      "name": "Background",
      "imageFile": "6F1D3C2A-0B7E-4E8A-9C4D-2A1B3C4D5E6F.png",
      "isVisible": true,
      "isGroup": false,
      "opacity": 1,
      "blendMode": "Normal",
      "transform": {
        "origin": [0, 0],
        "size": [1920, 1080],
        "rotation": 0,
        "flipX": false,
        "flipY": false,
        "sampling": "High quality"
      }
    }
  ]
}
```

- `layers` runs **bottom to top**: the last layer draws on top.
- Keep `documentID` as it is when editing an existing project.
- `transform` places the layer in document pixels: `origin` is its top-left corner, `size` its width and height, `rotation` is in degrees, clockwise. The image is stretched to `size`, so a layer can be smaller than the canvas (a cut-out placed with `origin`) or scaled.
- `sampling` is `"High quality"`, `"Smooth"` or `"Nearest"`.
- `opacity` runs from 0 to 1.

## Rules that matter

Break one of these and OmaPhoto refuses the whole file **without any message**: the open canvas just stays as it was. If nothing updates, check these first.

- **Image files are named after their layer.** A layer with `"id": "6F1D…"` must use `"imageFile": "6F1D….png"`, and a mask `"maskFile": "6F1D….mask.png"`, with the ID in uppercase as written in the manifest. One ID per layer, unique in the project.
- **Images are PNGs** in `images/`, at most 8 bits a channel: 16-bit PNGs are refused. Write layer images as 8-bit RGBA (RGB, grayscale and lower-depth grayscale load too, converted); masks are 8-bit grayscale (white shows the layer, black hides it).
- **Blend modes are spelled exactly** as OmaPhoto names them: `Normal`, `Darken`, `Multiply`, `Color Burn`, `Linear Burn`, `Lighten`, `Screen`, `Color Dodge`, `Linear Dodge (Add)`, `Overlay`, `Soft Light`, `Hard Light`, `Vivid Light`, `Linear Light`, `Pin Light`, `Hard Mix`, `Difference`, `Exclusion`, `Subtract`, `Divide`, `Hue`, `Saturation`, `Color`, `Luminosity`.
- **Every `imageFile` and `maskFile` the manifest names is in place**, and the manifest is valid JSON. Blank layers, folders and adjustment layers have no `imageFile`.

## Writing safely while the project is open

OmaPhoto reads the project as soon as it changes, so never leave it half written:

1. Write any new or changed PNGs into `images/` first.
2. Then write the manifest to a temporary file inside the project folder (for example `.manifest.json.tmp`) and rename it over `manifest.json`. A rename is atomic: OmaPhoto sees either the old manifest or the new one, never part of one.

To change an existing layer, keep its `id` and overwrite its PNG, then rewrite the manifest. The layer updates in place, in the same spot in the stack.

Remove images you no longer reference once the manifest no longer lists them.

## What the open app does

- It reloads about a third of a second after writes stop. Several writes in quick succession arrive as one update, so pause briefly between steps if a viewer should see each one.
- A reload keeps the zoom, scroll and selection, but clears undo, as reopening a file does.
- If the person has unsaved changes of their own, OmaPhoto asks them to revert to your version or keep theirs, and never replaces their work silently.
- A write that fails to load is ignored until the next change, so a mistake you then fix will still show up.
- Changes are noticed from the manifest's contents and from each image's name and size. Rewriting a PNG with different pixels changes its size in practice, but if you replace an image with one of exactly the same byte size, also change the manifest's bytes (any valid change will do, even added whitespace): rewriting identical bytes is not noticed.
- Replacing the whole project folder at once is noticed too. A populated folder cannot be renamed over another on Linux; exchange the two instead with `renameat2(RENAME_EXCHANGE)` (Python's `os.rename` cannot), then remove the old one.

## Masks

Add a mask to any layer with `"maskFile": "<id>.mask.png"` and `"maskEnabled": true`. The mask is stretched over the layer's box, so it usually has the same pixel size as the layer's image; a 1×1 mask covers the whole layer with one value. Soft grays give soft edges.

## Adjustment layers

An adjustment layer has an `adjustment` object and no `imageFile`, and it affects everything below it. Every kind carries identity `levels` and `curves` blocks, plus its own settings. A warming Curves layer:

```json
{
  "id": "A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D",
  "name": "Warm Grade",
  "isVisible": true,
  "isGroup": false,
  "opacity": 1,
  "blendMode": "Normal",
  "transform": { "origin": [0, 0], "size": [1920, 1080], "rotation": 0, "flipX": false, "flipY": false, "sampling": "High quality" },
  "adjustment": {
    "kind": "Curves",
    "hue": 0, "saturation": 0, "lightness": 0, "colorize": false,
    "levels": { "channel": "RGB", "ranges": [
      { "black": 0, "gamma": 1, "white": 255, "outputBlack": 0, "outputWhite": 255 },
      { "black": 0, "gamma": 1, "white": 255, "outputBlack": 0, "outputWhite": 255 },
      { "black": 0, "gamma": 1, "white": 255, "outputBlack": 0, "outputWhite": 255 },
      { "black": 0, "gamma": 1, "white": 255, "outputBlack": 0, "outputWhite": 255 } ] },
    "curves": { "channel": "RGB", "channels": [
      [ { "x": 0, "y": 0 }, { "x": 255, "y": 255 } ],
      [ { "x": 0, "y": 0 }, { "x": 120, "y": 147 }, { "x": 255, "y": 255 } ],
      [ { "x": 0, "y": 0 }, { "x": 100, "y": 114 }, { "x": 255, "y": 255 } ],
      [ { "x": 0, "y": 0 }, { "x": 115, "y": 97 }, { "x": 255, "y": 238 } ] ] }
  }
}
```

- `ranges` and `channels` run RGB, then red, green, blue. Curve points run from x 0 to x 255, in increasing x.
- `kind` is one of `Hue/Saturation`, `Levels`, `Curves`, `Exposure`, `Gradient Map`, `Grain`, `Invert`, `Black & White`, `Color Balance`, `Gaussian Blur`, `Motion Blur`, `Add Noise`.
- For Hue/Saturation, set `hue`, `saturation` and `lightness` on the adjustment itself when it has no `hsvSettings`. A Hue/Saturation layer saved by OmaPhoto carries `hsvSettings`, which wins over those three: there, edit its Master entry in `hsvSettings.adjustments`, a list of range names each followed by its values (for example `["Master", { "hue": 120, "saturation": 0, "lightness": 0 }]`), and leave the other ranges as they are. Color Balance takes a `colorBalanceSettings` object (`shadowCyanRed`, `shadowMagentaGreen`, `shadowYellowBlue`, and the same for `mid` and `highlight`, each −100 to 100, plus `preserveLuminosity`).
- For the other kinds, the easiest way to get the exact shape is to add one in OmaPhoto, save, and copy it from that project's manifest.

## More

- Folders, text layers, layer effects and everything else the format holds: [project-format.md](project-format.md).
- Limits: canvases up to 30,000 pixels on a side; layers and masks count toward a memory budget that scales with the computer's memory.
