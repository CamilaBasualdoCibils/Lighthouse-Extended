# Renderer infrastructure

The Extended renderer coordinates independently configured `RenderFeatureView`s.
Each view owns a render feature, a logical view family, and an `IRenderOutput`.
`RenderGraphComposer` and its `IOutputView`s continue to acquire and present images.
There is no renderer-wide draw batch.

This implementation provides interfaces, typed provider ownership, update calls,
graph assembly, destination mapping, and configuration lifetime management.
Forward rendering now includes a scene provider, shared `SceneRenderStore`, glTF
metallic/roughness material textures, scene-component lighting, depth attachments,
and SDR tone mapping/output resolve. Deferred rendering uses the same scene store,
materials, BRDF and HDRI inputs with a G-buffer followed by fullscreen lighting.
Path tracing, clustered light assignment, shadows and reflections remain scaffolds. `ResourceRequests`
remains a stub; scene allocations are cached in the shared store, not in it.

## One view

```cpp
auto rendererRef = composer.AddTechnique<Renderer>(&composer);
auto& renderer = *composer.GetTechnique<Renderer>(rendererRef);
auto sceneData = std::make_shared<SceneRenderStore>(scene);
auto& view = renderer.CreateView(std::make_shared<WindowRenderOutput>(window));
view.SetViewFamily({{RenderViewData{.id = 1}}});
auto& deferred = view.SetRenderFeature<DeferredRenderFeature>();
deferred.AddProvider<SceneDeferredRenderProvider>(sceneData);
// deferred.AddProvider<MyTerrainDeferredProvider>(terrain);
deferred.SetLighting<DefaultDeferredLightingProvider>(sceneData);
// deferred.SetShadows<MyDeferredShadows>();
// deferred.SetReflections<MyDeferredReflections>();
// deferred.AddCompositor<MySplatCompositor>(splats);
// deferred.AddDeferredPostProcess<MyDeferredEffect>();
deferred.AddDeferredPostProcess<Bloom>();

renderer.Update(deltaTime);
if (renderer.NeedsRebuild()) composer.Compile();
composer.Render();
```

`SetViewSource` can supply a fresh family during update/build. Matrices and poses
are live data and do not invalidate the graph. Changing view IDs, roles, grouping,
resolution, output, feature or registered providers does require recompilation.
Call `view.Invalidate()` after changing a custom provider setting that changes its
passes, output layout or pipeline. Feature/provider membership changes are tracked
automatically. Do not execute the old graph with a changed view topology.

Providers receive `RenderFeatureContext::family` as shared live state. Execution
callbacks needing moving cameras should capture that shared pointer, not a copy
of the matrices from graph construction. As with other graph contexts, do not
capture a reference to a temporary build context. Use stable IDs to key temporal
history inside each feature/provider; switching features creates a new instance.

## Features and providers

Each feature's contexts, semantics, provider interfaces and feature class live in
its own header: `ForwardRenderFeature.hpp`, `DeferredRenderFeature.hpp`, or
`PathTracingRenderFeature.hpp`. `Renderer.hpp` depends only on the minimal
`IRenderFeature` contract. Include the concrete feature headers you use.

- `DeferredRenderFeature`: multiple `IDeferredGeometryProvider`s, one each of
  lighting/shadows/reflections, multiple compositors and deferred post-processes.
- `ForwardRenderFeature`: corresponding forward contracts. Forward lighting
  prepares data consumed by geometry shading.
- `ForwardPlusRenderFeature`: forward contracts plus replaceable light assignment.
- `PathTracingRenderFeature`: multiple `IRaySceneProvider`s, light sampling,
  integrator, denoiser, compositors and path-tracing-specific post-processes.

All three concrete features implement `IRenderFeature` directly and own their
provider stages, revision aggregation and execution lifetimes. Forward providers
virtually inherit `IForwardProvider`; deferred providers virtually inherit
`IDeferredProvider`. Both roots virtually inherit `IRenderProvider`, so a class
supporting both techniques has one common provider subobject.

`IForwardPostProcess::BuildGraphForward(ForwardRenderContext&)` runs after forward
compositors and before the feature's final tone mapping/output resolution.
`IDeferredPostProcess::BuildGraphDeferred(DeferredRenderContext&)` is the separate
deferred contract. Deferred providers declare attachment/history requirements with
`GetDeferredDependencies()`. The feature gathers every registered provider's requests
before building geometry. Providers report dependency changes through
`GetConfigurationRevision()`, which invalidates the feature's cached graph.
Requested attachments are allocated and written by the G-buffer pass.
`ForwardSemantics` and `DeferredSemantics` describe independent auxiliary maps;
deferred temporal history is a separate map keyed by `DeferredHistory`. No feature
promises another technique's outputs. Path tracing retains its orchestration scaffold.

Scene adapters can share `SceneRenderStore`, retaining scene change tracking across
views and feature changes. Each provider owns its batching/render implementation;
custom providers can own different data layouts, resources and command recording.
`IForwardPreparationProvider` and `IDeferredPreparationProvider` are separate
contracts for generation/residency passes before their feature builds geometry. Providers add custom passes through the supplied
graph, with explicit reads/writes. Composite providers can read depth, GBuffer or
lit color as appropriate. Transparency ordering across providers is the responsibility
of their composition implementation.

`SceneProvider` (also named `SceneForwardRenderProvider`) records forward draws
using pipelines and binding layouts supplied by the feature. `DefaultForwardLightingProvider`
extracts enabled directional, point and spot `Light` components, including parent
transforms. The initial implementation uses up to 256 unshadowed lights, PNG/JPEG
textures, UV set zero and a single uploaded mip level. The default output resolver
supports SDR sRGB destinations. Ray tracing remains a scaffold.

`Resource::LoadScene(path)` parses a scene resource. `resource.GetDefaultScene()`
instantiates its default scene. Those operations remain CPU-side; the shared render store resolves
referenced meshes and images when the scene provider builds its rendering passes.

The RenderFeatures example displays the same DamagedHelmet scene with forward
rendering in the top-left quadrant and deferred rendering in the top-right. Both
share one SceneRenderStore, camera, lights and HDRI environment, with separate Bloom
instances. Forward and Deferred tabs expose their bloom settings; PathTrace and the
bottom quadrants remain empty. No duplicate scene mesh is added.

## Deferred rendering

Include `DeferredRendering.hpp` for `DeferredRenderServices` and
`DefaultDeferredLightingProvider`. Register `SceneDeferredRenderProvider` with the
same SceneRenderStore used by forward rendering. EnvironmentProvider and
HDRIIndirectLightingProvider explicitly support both raster provider roots; their
deferred contracts still remain separate from forward geometry/lighting interfaces.

The G-buffer contains linear albedo, world normal, world position, packed material
properties, emission and depth for the baseline material lighting pass. Providers
can additionally request `Motion`, `Metallic` and `Roughness`. These use RG16f,
R16f and R16f attachments respectively and are omitted when unrequested. All
requests are unioned across preparation, geometry, lighting, shadows, reflections,
compositors, post-processes, environment and indirect-lighting providers. Materials
compile the exact ordered output layout selected by the deferred feature. Lit and
Unlit materials write every selected output; packed properties identify which
shading model the fullscreen pass evaluates.

Motion stores current minus previous clip-derived UV coordinates. Previous model
and view/projection matrices are committed only after graph submission, keyed by
logical view and entity. First-frame and resized-view motion starts at zero, and
motion with invalid previous clip W is rejected. Moving cameras/objects use live
state without recompilation. Temporal *image* history remains separate and must be
supplied explicitly by a preparation provider if requested.

Deferred lighting evaluates the same surface BRDF, direct scene-light records and
split-sum HDRI lighting as forward. It preserves the sky wherever depth is clear,
then runs reflections, compositors and `BuildGraphDeferred` post-processes. The
same Bloom class processes the HDR result before the common scene-color resolver.
Bindings/frame buffers are immutable per submission and retired through RenderGraph.
No new per-frame waits or manually managed command-list Begin/End are introduced.

The scene G-buffer pass handles opaque and alpha-masked materials. Alpha-blended
materials require a transparency compositor and are explicitly rejected by that
pass rather than silently becoming opaque. Shadows/reflections still require
providers; this implementation does not invent those algorithms.

## Bloom

`Bloom` implements both post-process interfaces and shares one internal graph
builder. Install it with `forward.AddForwardPostProcess<Bloom>(BloomSettings{})`.
Both entry points share the same implementation; the deferred feature runs its
entry point after deferred lighting. It requires linear scene color, not a display-encoded target.

The graph renders highlight extraction with threshold/soft knee into a half-size
RGBA16f image. The first reduction uses grouped, luminance-weighted filtering
with a robust neighborhood limit to suppress isolated HDR outliers and keep them
from dominating the bloom pyramid. Non-finite samples are sanitized. Subsequent
levels use linear 13-tap downsample passes, tent-filtered upsample passes,
and a new full-size HDR composite. All chain images are transient RenderGraph
resources; image processing stays on the GPU. Dimensions round up and clamp at
one texel. Reconstruction is normalized to keep intensity independent of levels.

`GetSettings()` / `SetSettings()` expose `Enabled`, `Threshold`, `SoftKnee`,
`Intensity`, and `MipCount`. Enabled and level-count changes invalidate the graph.
Threshold, knee and intensity are supplied at execution time, using fresh uniform
buffers/bindings retired with the graph submission, with no extra waits. The graph
retains the provider and GPU state through execution. Disabling bloom adds no bloom
passes and preserves ordinary forward presentation.

## Outputs and multiple views

`IRenderOutput::BuildTargets` returns fresh graph mappings for the current build.
Each logical view maps to an image, array layer, mip, viewport, format and encoding.
Outputs describe final destinations; providers can allocate intermediate HDR images
and feature-specific auxiliary buffers. Each feature owns the conversion/composition
into its destinations and returns the **new graph resource versions**. Forward
rendering resolves its linear working color after all forward post-processes.
Preserve destination pixels outside a viewport for split-screen composition.

For split screen, create two renderer views using the same window handle and
separate `WindowRenderOutput(window, region)` regions. Each view selects its own
feature. The renderer chains output versions between views sharing an image.
The window remains one engine `IOutputView`.

For stereo/XR, use one family with stable left/right IDs, independently supplied
view/projection matrices, and `RenderViewGrouping::Stereo`. A `GraphRenderOutput`
factory maps the eyes to separate images or array layers. The XR integration owns
acquisition, per-frame eye poses/projections and submission. Eye separation is encoded
in those matrices; the renderer does not infer stereo from target image count.
The interface permits multiview; no multiview rendering implementation is supplied.

`GraphRenderOutput` also supports offscreen/export/custom targets. Its factory must
obtain/create resources for the current graph; never reuse handles from an old build.
`WindowRenderOutput` adapts an engine window and exposes its actual SDR/HDR encoding.

## Switching features

```cpp
view.SetRenderFeature<PathTracingRenderFeature>()
    .AddProvider<SceneRaytracedRenderProvider>(sceneData);
// Configure an integrator/denoiser/output resolver as needed.
renderer.Update(deltaTime);
composer.Compile();
```

The previous compiled graph retains its feature, providers and output objects until
that graph is replaced. Graph construction waits for prior GPU work before retirement.
This is intentionally conservative; fine-grained asynchronous retirement can replace
that wait later. Shared resource handles remain alive independently of feature choice.

## Shared material compilation

All surface rendering uses the existing `Materials`, `Material`, `MaterialInstance`
and `ShadingModel` system. Features select a compilation target; they do not define
an alternative surface-material language or parameter layout:

```cpp
auto material = materials.Load("materials/MySurface.material.json");
auto forward = material->Compile(MaterialRenderPath::Forward, lightingSource);
auto deferred = material->Compile(MaterialRenderPath::Deferred);
auto ray = material->Compile(MaterialRenderPath::RayTracing);
```

Each target uses the same material source, parameter schema, texture sampling API,
and `evaluateMaterial()` function. Lit shares its surface-response library between
forward and deferred lighting. Unlit bypasses incident lighting. The forward lighting
provider supplies `ForwardLightCount`, `ForwardLightAt`, `ForwardAmbientRadiance`,
and its set-2 incident-light buffer, rather than implementing the material's BSDF.

`materials/GltfPBR.material.json` is an ordinary Lit material definition. The scene
store translates imported factors and resolved textures into its regular material
instance options. It contains no separate forward surface evaluator. Custom authored
materials can be assigned to scene instances through a CPU resource handle:

```cpp
auto material = MaterialResource::FromDefinition("materials/MySurface.material.json",
    {{"color", Vec4(0.8f, 0.2f, 0.1f, 1.0f)}});
material.SetTexture("albedo", Resource::LoadTexture("albedo.png"));
entity.Get<MeshRender>().material = material;
```

CPU shader compilation for the ray target is independent of device RT support.
Creating a native material ray pipeline remains capability-gated. The current hit
library retains its existing color-payload/placeholder attribute contract; a complete
path tracer, real hit-attribute bindings and ray lighting/integration are not supplied
by this material compilation change.

## Environment and HDRI indirect lighting

Include `Lighthouse/Extended/Rendering/EnvironmentProviders.hpp`. Each forward
feature accepts one environment provider and one indirect-lighting provider.
Composition of multiple lighting techniques belongs inside a provider.

```cpp
auto hdri = std::make_shared<HDRIEnvironment>(Resource::LoadImage("sky.hdr"));
forward.SetEnvironmentProvider<EnvironmentProvider>(
    std::make_shared<HDRISkyProvider>(hdri));
forward.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(hdri);
```

Environment and sky compatibility is pipeline-specific:

- `IForwardEnvironmentProvider::GetForwardSky()` returns an `IForwardSkyProvider`,
  whose entry point is `BuildGraphForward(ForwardRenderContext&)`.
- `IDeferredEnvironmentProvider::GetDeferredSky()` returns an `IDeferredSkyProvider`,
  whose entry point is `BuildGraphDeferred(DeferredRenderContext&)`.

Neither interface inherits the other pipeline's provider root. `HDRISkyProvider`
explicitly implements both sky interfaces. The concrete `EnvironmentProvider`
adapter implements both environment interfaces and accepts only a sky that
implements both contracts. Pipeline-specific environment implementations can
inherit only the interface they actually support; incompatible registration is
rejected by the feature's template constraints. Virtual inheritance keeps one
common provider lifecycle for implementations that deliberately support both.
Sky uses live projection and view rotation; camera translation does not move it.

`IForwardIndirectLightingProvider` prepares graph dependencies and contributes GLSL
plus its descriptor layout/set at set 3. Its shader exposes
`ForwardDiffuseIrradiance(position, normal)`,
`ForwardEnvironmentRadiance(position, direction, roughness)`, and
`ForwardEnvironmentBRDF(nv, roughness)`. `IDeferredIndirectLightingProvider` is a
separate contract using `DeferredRenderContext`; the concrete
`HDRIIndirectLightingProvider` explicitly implements both and shares its internal
resource preparation. Lit owns albedo, metallic, Fresnel and the surface response.
Unlit bypasses these contributions.

Specular IBL uses a cached GGX-prefiltered equirectangular mip chain and a 128x128
BRDF integration LUT. Each prefilter level represents a known roughness; the
material selects/interpolates levels using roughness and combines radiance with
the LUT. There is no per-fragment stochastic integration loop. During prefiltering,
GGX sample PDF and latitude-dependent texel solid angle select ordinary source
mips, reducing high-frequency aliasing from bright HDR features. The default
`HDRIIndirectLightingOptions::specularResolution` is 512x256 and
`specularSamples` is 1024; both are ordinary API options. Prefiltering stops at
16x8 for a 2:1 map to retain directional information at roughness one. The sky
continues to sample the original HDRI, not its rough reflection convolution.

The default indirect option computes a 64x32 irradiance map with 1024
cosine-weighted samples per texel on the CPU when first needed. The result is
cached, not recomputed per frame. Importing a map is explicit:

```cpp
HDRIIndirectLightingOptions options;
options.mode = IrradianceMode::Import;
options.irradiance = Resource::LoadImage("sky.irr.hdr");
forward.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(hdri, options);
```

No sibling `.irr.hdr` is searched for or automatically loaded. Both maps use linear
RGBA32f, equirectangular coordinates (longitude `atan(z,x)`, north at the first row).
Imported irradiance is physical irradiance, the integral of incoming radiance
times cosine; Lit divides diffuse irradiance by pi. `ComputeHDRIIrradiance` exposes
the same CPU convolution for other callers.

`Resource::LoadImage` still creates a lazy handle. `Image::Resolve()` explicitly
uses the vendored stb_image decoder for PNG/JPEG (RGBA8) and Radiance RGBE HDR
(linear RGBA32f), preserving values
above one. It also handles memory, generator and glTF sources. A requested format
must match the decoded format; conversion is not implicit. `flipY` works for both
byte and float images. HDRI resources share decoded pixels and GPU textures across
sky and IBL. `Image::MarkDirty()` and updates invalidate the provider revision,
causing graph rebuild, re-upload and irradiance regeneration on the next update.
Both graphics backends upload byte/half/float mip data directly. Texture views
expose the entire supplied chain. IBL prefiltering and BRDF integration use cached
initialization draws on graphics queue zero and wait for completion once; source
revision changes invalidate the environment convolution cache.
Render devices must outlive providers and compiled graphs using their resources.

Forward materials without lighting providers now receive zero ambient and zero
indirect light. `DefaultForwardLightingProvider` defaults its optional legacy
constant ambient argument to zero; a caller can still explicitly request one.

The example loads `/home/camila/Downloads/HDRIs/Newport_Loft_Ref.hdr` through
`Resource::LoadImage` and explicitly selects `IrradianceMode::Compute` in its
`HDRIIndirectLightingOptions`. Source images, computation settings and imported
irradiance maps are configured through the C++ API shown above.

The asset tests cover HDR decoding, malformed RLE, float row flipping, laziness,
cache invalidation and constant-radiance irradiance normalization. To exercise
local Newport assets and the offscreen helmet tests, set `LIGHTHOUSE_TEST_HDRI`
to the source HDR file. Add `LIGHTHOUSE_TEST_IRRADIANCE` for the import path, or
leave it unset to test computation. Run `LighthouseExtendedForwardTests` for
OpenGL and add `--vulkan` for Vulkan. These tests disable direct lights, compare
IBL against the same sky without IBL, and verify quadrant preservation.

## Image mip policies and data

```cpp
auto image = Resource::LoadImage("textures/brick.png", ImageLoadOptions{
    .mipPolicy = MipPolicy::Generate
});
```

`ImageLoadOptions::mipPolicy` defaults to `Auto`. Loading remains lazy:

- `None`: retain only the base level.
- `Load`: retain levels present in the asset; a base-only asset stays base-only.
- `Generate`: discard authored lower levels and generate a complete chain.
- `Auto`: preserve an authored chain; generate when only the base is available.

stb formats such as PNG/JPEG/HDR contain a base level. The loader also accepts
uncompressed, little-endian KTX1 2D textures with RGBA8, sRGB RGBA8, or RGBA32f
pixels, including authored mip levels. Compressed KTX, KTX2, DDS, arrays and cubes
are not decoded by this implementation. `flipY` applies to every supplied mip.

`ImageData` owns a public `format` and `std::vector<ImageMipData> mips`; each mip
owns `width`, `height`, `depth` and tightly packed `data`. Levels must halve each
non-unit dimension, and supplied chains may be partial. The single-base-level
constructor and `GetResolution()/GetData()` remain compatible; pass a level index
to inspect another mip. `GetMipCount()`, `Validate()` and `ToImageDesc()` support
the full chain. Memory/generator resources preserve the mip data they provide;
call `ApplyImageMipPolicy` to request generation explicitly. `GetMipCount()` counts
CPU-authored levels; `GetRequiredMipCount()` counts the GPU allocation, including
requested generated levels. `generateMipmaps` records that upload must generate them.

Ordinary mip generation runs on the GPU at texture upload. The render command list
exposes `GenMipMaps(GenMipMapsDesc{.image = image})`, generating all allocated levels
from level zero across every layer, outside a rendering pass. OpenGL uses
`glGenerateTextureMipmap`; Vulkan uses successive linear image blits with per-level
layout transitions, finishing in shader-read layout. Supply a single-sampled,
linearly filterable color image with a full mip allocation. Standard backend
filtering applies, including linear-space filtering for sRGB textures.
Ordinary mip levels provide
sampling footprints; they are not interchangeable with GGX roughness convolution.

`LighthouseExtendedMipTests` (or `--vulkan`) verifies authored byte/float mip
uploads, nonzero LOD sampling, GGX blur and constant-radiance preservation, plus
prefilter/LUT caching. CPU mip tests verify policies, authored KTX levels,
non-power-of-two dimensions, volume filtering and HDR/sRGB averaging.
