# Mobile Forward-Rendering Water <img align="right" width=128, height=128 src="https://github.com/Vaei/MobWater/blob/main/Resources/Icon128.png">

> [!IMPORTANT]
> Water for mobile forward rendering path
> <br>Ponds, lakes, pools and ocean, that a character wades into and swims in
> <br>Ripples, wakes and splashes from anything that moves through it
> <br>And waves a dedicated server evaluates to the same answer the client draws
> <br>Designed for the lightweight Mobile Forward Rendering pathway

**Suitable for realistic and stylized projects alike**

UE5.8+

---

> [!CAUTION]
> Shore line support for oceans, waterfalls, and several major features have not yet been added at all

> [!CAUTION]
> MobWater has not officially released. Expect terrible bugs, and updates without versioning or a changelog reflecting them. The documentation is written but there are no images or videos in it yet. **Come back soon!**

<!-- TODO(image): hero shot - a temple basin at dusk, a character stepping in, rings going out -->

## Documentation

**[vaei.github.io/MobWater](https://vaei.github.io/MobWater/)**

Or open [`docs/index.html`](docs/index.html) from a clone - it is a static site with no build step and no network, so it works straight off disk.

| | |
|---|---|
| [Install](https://vaei.github.io/MobWater/install.html) | clone, generate the materials, place a pool |
| [Bodies of water](https://vaei.github.io/MobWater/bodies.html) | pool, lake, river, ocean - which one a thing is |
| [Waves](https://vaei.github.io/MobWater/waves.html) | Gerstner, baked spectrum, and the clock they share |
| [Techniques](https://vaei.github.io/MobWater/techniques.html) | depth, foam, refraction, exclusion - when each is the right answer |
| [Stylized](https://vaei.github.io/MobWater/stylized.html) / [Realistic](https://vaei.github.io/MobWater/realistic.html) | the two art directions, with numbers |
| [Surface](https://vaei.github.io/MobWater/surface.html), [Ripples](https://vaei.github.io/MobWater/ripples.html), [Exclusion](https://vaei.github.io/MobWater/exclusion.html), [Underwater](https://vaei.github.io/MobWater/underwater.html) | every parameter |
| [Characters in it](https://vaei.github.io/MobWater/characters.html) | wading, swimming, splashing |
| [Ships in it](https://vaei.github.io/MobWater/queries.html) | the CPU query, and what it deliberately does not know |
| [Cost](https://vaei.github.io/MobWater/performance.html) | fill, instructions, permutations |
| [If it is wrong](https://vaei.github.io/MobWater/troubleshooting.html) | symptom to cause |

## Why

Water that shallows at the bank, foams where it meets the shore, and shows the bed through it. Waves that lie down as they reach the shore instead of cutting through it. Ripples that spread from anything moving in the water and reflect off whatever is standing in it.

Boats sit at the right height on the client and the server both.

All of it costs one translucent plane, and runs anywhere - including the mobile forward path, where most water does not.

## Features

### Water that knows how deep it is

- **The bed comes from the depth buffer**, so the water column's thickness is a tap that was already being paid for. Colour, clarity, shoreline foam and the underwater fade all read off that one number
- **Waves lie down as the bottom comes up.** Amplitude is weighted by depth, so displacement dies at the bank instead of clipping through it. This is the single term that stops a lake's edge shimmering above dry ground
- **Foam where the water actually meets something** - the shore from the depth gradient, the crests from wave steepness. A still pond gets the first and not the second, which is why it reads as still
- **Refraction is optional and compiles out.** With it off there is no scene colour read at all, which is the right look for deep or stylized water and the only look on a platform that will not copy scene colour

### Any style, from one material

- **Absorption or a gradient.** Beer-Lambert against depth for hyperreal, or a ramp indexed by depth for stylized - hard bands, any palette, from the same tap and the same instruction budget. The ramps are a [GradientTool](https://github.com/Vaei/GradientTool) asset, so a palette is dragged rather than recompiled
- **Foam takes the same fork** - noise-broken and dithered, or stepped to hard contours
- **Every parameter is per body**, so a jade temple pool and a grey harbour are the same material and share every permutation

### Waves a server can agree with

- **The wave field is a function of position and time**, never a simulation, so two machines given the same instant get the same surface. Buoyancy on the client and buoyancy on the server are the same arithmetic
- **Nothing is simulated.** Waves are Gerstner sums evaluated from position and time, with no compute shader, no readback and no frame of latency - which is what lets a dedicated server with no GPU answer the same question
- **A baked FFT sea state for the ocean.** A Phillips spectrum solved offline and sampled rather than summed, because a handful of sines can only ever look like a handful of sines. It loops exactly - every component is rounded to a whole number of turns over the period, so there is no crossfade to find - and the query reads the same bytes the shader samples, out of a table rather than a texture, because a dedicated server has no texture
- **Buoyancy.** A pontoon component that floats a rigid body on the surface both machines compute. Its coefficient is a multiple of the body's own weight, so the equilibrium is arithmetic rather than tuning: at rest the pontoons settle at 1/Coefficient submerged
- **More than one sea state a level.** A baked sea's layout is per body rather than per world, so a harbour on a short chop and the open sea beyond it on a long swell are two assets and two oceans rather than a choice of which one the level gets
- **Bring your own clock.** Water time is an input. Bind a synchronised clock and the waves are in phase everywhere; leave it unbound and it falls back to the engine's, which is enough to see water move and not enough to keep two machines together
- **Asserted on three machines, not argued.** A real dedicated server and two real clients each record the surface at fixed points every frame and the files are compared row by row - then again with the clock stepped by a thousand loop periods, which is what proves the fold, and again with two machines deliberately out of phase, which have to disagree
- **A Rewind Debugger track.** The clock, the wave set and every body's transform, plus every query attributed to whoever made it - because a ship that desyncs has usually not found different water, it asked about a different place, and those are unrelated bugs
- **The CPU cost is opt in per body**, so a decorative ocean pays nothing for a table it never reads

### Things moving through it

- **Ripples from anything that moves**, spreading and reflecting through a real wave equation rather than a scrolling texture
- **A wake behind anything that keeps moving**, and foam left where it has been
- **Splashes at the feet**, at the depth the character is actually standing in
- **Wading and swimming** - drag that grows with immersion, and a swim state above it
- **One field, however many things are in it.** Ripples and foam share one camera-following target, so its cost is fixed by resolution and a second water body reading it is not a second cost

### Water kept out

- **Exclusion volumes in five shapes** - disc, sphere, box, rect and mesh. A hull is dry inside and clear outside, and stays that way while it moves
- **A mesh cuts its own outline**, rasterised once in the editor to a distance mask, so a cooked build never reads a vertex buffer and a soft edge means the same centimetres it does on a box
- **Exclusion is both**, carving the rendered surface and blocking submersion, so nothing has to be authored twice to agree - and the two are compared against each other at a grid of points rather than assumed to match

### Under it

- **Absorption that hides distance** rather than only tinting it, marched the same way the surface is shaded
- **Caustics on what is submerged**, projected from a volume that reads the same depth buffer - a light that casts a texture instead of a falloff
- **The line at the surface**, from underneath, where the world above goes to reflection
- **Snell's window**, the whole world above compressed into the forty-nine degree cone the surface lets through, ringed by the bright compressed horizon. Its edge is the full Fresnel transmittance reaching zero at the critical angle, not a fade - Schlick has no critical angle and would leave the rim passing a third of the sky. Filled from the sky the surface already reflects, for one texture read, or from a scene capture of the world above where the level's own shoreline should be in the disc
- **It attaches itself**, to whichever camera is drawing the picture - the player's, a debug camera the moment it takes over, an editor viewport when nothing is playing
- **The camera going under is its own event**, separate from any character going under, because in anything but a first person game they happen at different moments in different places. Bind `OnViewSubmergedChanged` for what belongs to a lens - droplets on surfacing, spatter, a muffle - and it carries how deep the camera had been, which is the number depth cannot give you once it has surfaced

### Built for the mobile forward path

- **No renderer code.** No scene view extension, no render graph pass, no global shader. A water body is a mesh with a material, the way a MobLight is
- **A disabled feature is not compiled.** Its texture samples and its maths leave the shader entirely
- **Camera-relative throughout**, because a UE5 world position cannot be squared without losing it and the mobile pixel ALU is half precision besides

## Quick start

```
cd YourProject/Plugins
git clone git@github.com:Vaei/MobWater.git
git clone git@github.com:Vaei/GradientTool.git
```

[GradientTool](https://github.com/Vaei/GradientTool) is required - it holds the colour ramps the stylized fork grades along. `MobWater.uplugin` names it, so Unreal enables it for you and will not load MobWater without it.

1. Build, enable the plugin, restart.
1. **Water → Generate Materials.**
1. Drag a **Water Pool** out of the **Water** menu.
1. Set its extent and depth, and drop it into a hollow.
1. Walk into it.

Full walkthrough: [Install](https://vaei.github.io/MobWater/install.html).

## Resources

> [!NOTE]
> While Mobile Forward Rendering can have serious limitations regarding lighting, it is suitable for **many** games, not merely Valorant/TF2 clones
> <br>And these games can look stunningly beautiful and crisp, while retaining ~1000fps in packaged builds

* [Mobile Forward Rendering Guide](https://blog.daftsoftware.com/unreal-perf-maxing/)
  * Includes Convenient Starter Project
* [MobMaterials Plugin](https://github.com/Vaei/MobMaterials)
  * Landscape, surface and foliage masters for the same renderer, which is what this water sits in and wets
* [MobLights Plugin](https://github.com/Vaei/MobLights)
  * Local lights for a renderer that has none, and fog to put them in
* [MobFort Plugin](https://github.com/Vaei/MobFort)
  * Stylized unlit character masters, so the characters standing in this water read the same anywhere
* [Gradient Tool Plugin](https://github.com/Vaei/GradientTool) - **required**
  * Stylized water is a gradient indexed by depth, and a gradient you cannot edit without recompiling is not a gradient
* [Tessendorf, *Simulating Ocean Water*](https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf)
  * The Phillips spectrum, the choppy-wave transform and the Jacobian the foam comes from. What `mob_water_spectrum.py` implements, with the frequencies quantised so the result loops
* [Non-Destructive Synced Net Clock](https://vorixo.github.io/devtricks/non-destructive-synced-net-clock/)
  * What to bind to the time source. A clock corrected by rate rather than by snapping, which is the difference between waves that stay in phase and waves that jump every correction
* [Forward Render Helper Plugin](https://github.com/Vaei/ForwardRender)
  * Replaces Hotpatch Module in Perf Maxing guide above

## License

MIT. The documentation site bundles IBM Plex Sans and Mono under the SIL Open Font License 1.1.
