# Constellations

Animated pattern backgrounds, borders, overlays and scene transitions for [OBS Studio](https://obsproject.com/) 33 and later.

## What's included

| Name | Type | What it does |
|------|------|--------------|
| **Pattern Background** | Source | A canvas of tiled shapes, images or other sources on a solid or gradient background. |
| **Pattern Filter** | Filter | The same patterns drawn over any source. |
| **Pattern Border** | Filter | A frame of shapes around a source, inside its edges or grown outside them, with rounded corners. |
| **Skylines Background** | Source and filter | A flat-look city skyline with lit windows, drift and window twinkle. |
| **Text Tiles Background** | Source and filter | A phrase repeated in rows across the canvas, each row sliding at its own speed and direction. |
| **Tiling Transition** | Transition | The screen is divided into a lattice of shapes that animate in sequence to carry you to the next scene. |
| **Vortex Transition** | Transition | A spiral portal tears open over the outgoing scene, swallows the canvas, and lets the incoming one in through the smoke. |
| **Glass Transition** | Transition | The outgoing scene cracks from a point of impact, holds a beat, and the shards fall away. |
| **Matte Cutout Transition** | Transition | One big shape cuts the outgoing scene away, with optional border, echo bands and glow for a stinger look. |
| **Portal Transition** | Transition | A portal is shot open with the incoming scene showing through it, then grows to swallow the canvas. |

Pattern items can be built-in shapes (circle, cross, square, polygon, squircle, lines, star, heart), images (PNG, JPG,
SVG and more, plus built-in SVG presets) or any other OBS source. Up to 32 items can be layered, alternated in a step
and repeat, or scattered across a grid. Each item can have its own size, rotation, offset, density and colors (including
random per-cell colors). Patterns can scroll, wander, twinkle and rotate, and a vignette can fade them out.

Text Tiles repeats one phrase (with an optional separator such as • or ★) along rows, in any installed font picked
through the standard OBS font dialog. Rows can all move the same way, alternate, or pick a random direction each, with
per-row speed variation and an optional slow speed wander so rows drift past each other. A row stagger gives the
stair-step look, and the whole pattern can be rotated. The text can be a solid color, a gradient across the canvas or
along each word, or tone-on-tone (darkening or lightening whatever is behind it), filled or as outlines only. As a
source it draws over a solid, linear or radial gradient background, or a transparent one; as a filter it draws over
the filtered source.

The transitions are generated on the GPU with no bundled video; add them in OBS under **Scene Transitions → +**. See
[docs/transitions.md](docs/transitions.md) for every transition's settings.

The transitions used to ship as a separate plugin, Tiles. They keep the same IDs, so scene collections that use them
carry over: install Constellations, then remove the old `tiles` plugin so the transitions aren't registered twice.

## Installing

Download the package for your platform from the [releases](https://github.com/voidscape-development/constellations/releases).

Since OBS Studio 33, third-party plugins on Windows are installed to
`C:\ProgramData\obs-studio\plugins\constellations\constellations.dll` with resources in `constellations\data`; portable
OBS installs use `plugins\constellations` in the OBS folder. System-wide Linux packages install to
`<libdir>/obs-modules/plugins`. Plugins left in the older locations still load in OBS Studio 33 but are marked as legacy,
and stop loading in OBS Studio 34.

## Building

The project uses the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate) build system, and CMake
presets cover each platform:

```sh
cmake --preset ubuntu-x86_64   # or: macos, windows-x64
cmake --build --preset ubuntu-x86_64
```

| Platform | Requirements |
|----------|--------------|
| Windows | Visual Studio 18 2026, CMake 3.30.5 |
| macOS | Xcode 26.5, CMake 3.30.5 |
| Ubuntu 26.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential`, OBS Studio 33 development files |

See the plugin template wiki for the
[build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options).

### Tests

The placement math (`src/lattice.c`), the Text Tiles row motion (`src/text-tiles-math.c`) and the transition math
(`src/*-math.c`, `src/glass-mesh.c` and the shaders' `data/effects/*_math.effect`) have unit tests that need no OBS SDK:

```sh
cmake -S tests -B build_tests -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

The transition effects also have checks that render on a real graphics device, built with the plugin. On a headless
machine run them under Xvfb:

```sh
cmake --preset ubuntu-x86_64 -DENABLE_GPU_TESTS=ON
cmake --build --preset ubuntu-x86_64
xvfb-run -a ctest --test-dir build_x86_64 --output-on-failure -L gpu
```

[docs/transitions.md](docs/transitions.md#how-the-transitions-are-tested) describes what each test covers.

### Formatting

C sources follow `.clang-format` (clang-format 19.1.1) and CMake files follow `.gersemirc`; CI checks both.

## How it works

A few design notes for contributors:

- **One quad per shape.** `data/effects/pattern.effect` draws each shape as its own small quad. The draw passes no
  vertex buffer: the vertex shader turns the vertex id into a lattice cell (or a spot on a border row) and computes
  its position, rotation, color and twinkle there. The pixel shader evaluates a single shape. Cost follows the area
  the shapes cover, and any shape size works at any density.
- **Nothing grows without bound.** Scrolling, twinkle and drift are integrated in double precision on the CPU and
  handed to the GPU as wrapped whole cells plus small remainders (`src/lattice.c`, unit tested). Per-cell randomness
  is keyed on the wrapped indices, so patterns stay smooth and stable on streams that run for days.
- **Settings never race rendering.** `update()` (UI thread) parses settings into a new configuration and decodes any
  new artwork. `video_tick()` (graphics thread) swaps it in and uploads textures, so a frame never sees a half-applied
  change or a texture being replaced.
- **Color.** Everything is drawn as linear-light, premultiplied color with the sRGB framebuffer enabled, matching
  OBS's own sources, and filters keep their target's color space, including HDR. Images are premultiplied in linear
  light and get a full mip chain, so they stay clean when drawn small or moving.
- **Sources inside patterns.** Sources used as pattern items are referenced by UUID (renaming them keeps the link),
  are shown and activated while the pattern is (so media plays and browser sources render), and a pattern can never
  end up drawing itself.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Bundles [nanosvg](https://github.com/memononen/nanosvg) (zlib license, see
`deps/nanosvg/LICENSE.txt`).
