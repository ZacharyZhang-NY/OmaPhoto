# OmaPhoto

OmaPhoto is a layered image editor for Linux, made first for [Omarchy](https://omarchy.org). It follows your Omarchy theme and switches with it.

It is a Linux port of [Compositor](https://github.com/robbietilton/Compositor) by Wonder Assembly, a macOS app written in Swift. Compositor's source is the specification: OmaPhoto keeps its tools, its menus and its shortcuts (Ctrl for ⌘, Alt for ⌥). It reads and writes Compositor's `.comp` projects, so a project moves between the two apps. OmaPhoto is independent of Wonder Assembly.

## What it does

- Layers, folders, masks, clipping masks, blend modes and opacity; whole layers copy between projects
- Selections: marquee, lasso, magic wand and its object mode, Select › Subject, load from a layer, feather, expand and contract
- Brush, eraser, spot healing, clone stamp, smudge, blur and liquify, gradient, shapes, type with colour on selected letters
- Move, transform and distort layers and selections; rulers, guides, a grid and snapping
- Levels, Curves, Hue/Saturation, Exposure, Black & White, Color Balance, Gradient Map, Grain, Invert, blur and noise, as edits or as adjustment layers
- Camera Raw Filter, docked beside the canvas
- Layer effects: stroke, drop shadow, outer and inner glow, color overlay, inner shadow
- Filters: Gaussian and motion blur, noise, lens correction, Vignette, Bloom / Glow, Tonal Contrast, Dither, Content-Aware Fill and Remove Background (U²-Net on ONNX Runtime, offline)
- Crop, trim, canvas size, image size; PNG and JPEG export
- Imports JPEG, PNG, TIFF, HEIC, SVG, Photoshop files (PSD and PSB) and camera RAW
- Open Recent, and a Keyboard Shortcuts window
- Drag a number's label to scrub its value
- Keep working while a project saves
- A project another app changes on disk reloads in place; unsaved work is never replaced without asking

### Works with AI agents

AI agents and scripts can build and edit projects directly: a `.comp` is a folder of PNG layers and a manifest, and an open project updates live as it's written. See [Writing OmaPhoto projects](docs/writing-comp-files.md) and the [project format](docs/project-format.md).

## Install

Each script installs the newest release. The Arch, Ubuntu and Fedora scripts download its package, check it against the release's `SHA256SUMS` and install it with your system's package manager; the NixOS script builds the release's flake. Packages are built for x86_64.

Arch and Omarchy:

```sh
curl -fsSLO https://raw.githubusercontent.com/ZacharyZhang-NY/OmaPhoto/main/scripts/install/arch.sh && bash arch.sh
```

Ubuntu 24.04 or 26.04 LTS and systems built on them (Linux Mint 22, Pop!_OS 24.04):

```sh
curl -fsSLO https://raw.githubusercontent.com/ZacharyZhang-NY/OmaPhoto/main/scripts/install/ubuntu.sh && bash ubuntu.sh
```

Each DEB names its Ubuntu release's libraries, so other Ubuntu releases and Debian build from source (below).

Fedora 43 or 44:

```sh
curl -fsSLO https://raw.githubusercontent.com/ZacharyZhang-NY/OmaPhoto/main/scripts/install/fedora.sh && bash fedora.sh
```

Fedora's own libheif decodes no HEVC. For HEIC import, add [RPM Fusion](https://rpmfusion.org/Configuration) and install `libheif-freeworld`; the script reminds you.

NixOS installs the release's flake into your profile:

```sh
curl -fsSLO https://raw.githubusercontent.com/ZacharyZhang-NY/OmaPhoto/main/scripts/install/nixos.sh && bash nixos.sh
```

Or add `github:ZacharyZhang-NY/OmaPhoto/<tag>`, a release tag, as a flake input and put its `packages.x86_64-linux.default` in your configuration.

The packages themselves sit on the [releases page](https://github.com/ZacharyZhang-NY/OmaPhoto/releases).

## Build from source

The build runs in Docker, so the host needs only Docker:

```sh
scripts/dev.sh build   # configure and build in Ubuntu 24.04
scripts/dev.sh test    # every test, headless
scripts/dev.sh run     # build, then the app on your Wayland display
```

`run` gives the container your home folder at its own path, so the app opens and saves your files and reads your Omarchy theme, and the GPU's render nodes (`/dev/dri/renderD*`), which Mesa's EGL wants. Files outside your home folder stay out of reach.

By hand: C++20, Qt 6.4 or later (Widgets, Concurrent, image formats), CMake, Ninja, libheif, LibRaw, fontconfig and ONNX Runtime. `-DOMAPHOTO_MODEL=path/to/u2net.onnx` installs Remove Background's model. `scripts/distro-check.sh arch|fedora|nixos|resolute` builds and tests on those systems (`resolute` is Ubuntu 26.04).

## Licence

MIT, as Compositor is; see `LICENSE`. Remove Background uses U²-Net (Apache-2.0, `licenses/U-2-Net.txt`) through ONNX Runtime (MIT). Every package carries ONNX Runtime with its notices.
