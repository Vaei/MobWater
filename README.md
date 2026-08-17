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
> Shore line support for oceans and waterfalls have not yet been added at all

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

- **Absorption or a gradient.** Beer-Lambert against depth for hyperreal, or a ramp indexed by depth for stylized - hard bands, any palette, from the same tap and the same instruction budget
- **Foam takes the same fork** - noise-broken and dithered, or stepped to hard contours
- **Every parameter is per body**, so a jade temple pool and a grey harbour are the same material and share every permutation

### Waves a server can agree with

- **The wave field is a function of position and time**, never a simulation, so two machines given the same instant get the same surface. Buoyancy on the client and buoyancy on the server are the same arithmetic
- **Nothing is simulated.** Waves are Gerstner sums evaluated from position and time, with no compute shader, no readback and no frame of latency - which is what lets a dedicated server with no GPU answer the same question. The baked FFT spectrum the high tier is designed around is not written yet
- **Bring your own clock.** Water time is an input. Bind a synchronised clock and the waves are in phase everywhere; leave it unbound and it falls back to the engine's, which is enough to see water move and not enough to keep two machines together
- **The CPU cost is opt in per body**, so a decorative ocean pays nothing for a table it never reads

### Things moving through it

- **Ripples from anything that moves**, spreading and reflecting through a real wave equation rather than a scrolling texture
- **A wake behind anything that keeps moving**, and foam left where it has been
- **Splashes at the feet**, at the depth the character is actually standing in
- **Wading and swimming** - drag that grows with immersion, and a swim state above it
- **One field, however many things are in it.** Ripples, foam and exclusion share one camera-following target, so its cost is fixed by resolution and a second water body reading it is not a second cost

### Water kept out

- **Exclusion volumes in five shapes** - disc, sphere, box, rect and mesh. A hull is dry inside and clear outside, and stays that way while it moves
- **Exclusion is both**, carving the rendered surface and blocking submersion, so nothing has to be authored twice to agree

### Under it

- **Absorption that hides distance** rather than only tinting it, marched the same way the surface is shaded
- **Caustics on what is submerged**, projected from a volume that reads the same depth buffer - a light that casts a texture instead of a falloff
- **The line at the surface**, from underneath, where the world above goes to reflection

### Built for the mobile forward path

- **No renderer code.** No scene view extension, no render graph pass, no global shader. A water body is a mesh with a material, the way a MobLight is
- **A disabled feature is not compiled.** Its texture samples and its maths leave the shader entirely
- **Camera-relative throughout**, because a UE5 world position cannot be squared without losing it and the mobile pixel ALU is half precision besides

## Quick start

```
cd YourProject/Plugins
git clone git@github.com:Vaei/MobWater.git
```

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
* [Gradient Tool Plugin](https://github.com/Vaei/GradientTool)
  * Stylized water is a gradient indexed by depth, and a gradient you cannot edit without recompiling is not a gradient
* [Non-Destructive Synced Net Clock](https://vorixo.github.io/devtricks/non-destructive-synced-net-clock/)
  * What to bind to the time source. A clock corrected by rate rather than by snapping, which is the difference between waves that stay in phase and waves that jump every correction
* [Forward Render Helper Plugin](https://github.com/Vaei/ForwardRender)
  * Replaces Hotpatch Module in Perf Maxing guide above

## License

MIT. The documentation site bundles IBM Plex Sans and Mono under the SIL Open Font License 1.1.
