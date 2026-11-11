# Renderer infrastructure

The Extended renderer coordinates independently configured `RenderFeatureView`s.
Each view owns a render feature, a logical view family, and an `IRenderOutput`.
`RenderGraphComposer` and its `IOutputView`s continue to acquire and present images.
There is no renderer-wide draw batch.

This implementation provides interfaces, typed provider ownership, update calls,
graph assembly, destination mapping, and configuration lifetime management.
Forward rendering now includes a scene provider, shared `SceneRenderStore`, glTF
metallic/roughness material textures, scene-component lighting, depth attachments,
and SDR tone mapping/output resolve. Deferred/path-tracing rendering, clustered
light assignment, shadows and reflections remain scaffolds. `ResourceRequests`
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
// deferred.SetLighting<MyDeferredLighting>();
// deferred.SetShadows<MyDeferredShadows>();
// deferred.SetReflections<MyDeferredReflections>();
// deferred.AddCompositor<MySplatCompositor>(splats);
// deferred.AddDeferredPostProcess<MyDeferredEffect>();
// deferred.AddPostProcess<MyCommonEffect>();
// deferred.SetOutputResolver<MyToneMapAndResolve>();

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

- `DeferredRenderFeature`: multiple `IDeferredGeometryProvider`s, one each of
  lighting/shadows/reflections, multiple compositors and deferred post-processes.
- `ForwardRenderFeature`: corresponding forward contracts. Forward lighting
  prepares data consumed by geometry shading.
- `ForwardPlusRenderFeature`: forward contracts plus replaceable light assignment.
- `PathTracingRenderFeature`: multiple `IRaySceneProvider`s, light sampling,
  integrator, denoiser, compositors and path-tracing-specific post-processes.

Common `IRenderPostProcess` effects run after feature-specific effects and before
the output resolver. They can declare required auxiliary inputs; missing inputs
fail graph construction. Feature-specific effects have typed contexts and cannot
be installed on incompatible features. Auxiliary outputs must document their
encoding/space; names alone do not make arbitrary implementations interchangeable.

Scene adapters can share `SceneRenderStore`, retaining scene change tracking across
views and feature changes. Each provider owns its batching/render implementation;
custom providers can own different data layouts, resources and command recording.
`IRenderPreparationProvider` is for generation/residency passes needed before the
feature builds its geometry. Providers add custom passes through the supplied
graph, with explicit reads/writes. Composite providers can read depth, GBuffer or
lit color as appropriate. Transparency ordering across providers is the responsibility
of their composition implementation.

`SceneProvider` (also named `SceneForwardRenderProvider`) records forward draws
using pipelines and binding layouts supplied by the feature. `DefaultForwardLightingProvider`
extracts enabled directional, point and spot `Light` components, including parent
transforms. The initial implementation uses up to 256 unshadowed lights, PNG/JPEG
textures, UV set zero and a single uploaded mip level. The default output resolver
supports SDR sRGB destinations. Deferred geometry and ray tracing remain stubs.

`Resource::LoadScene(path)` parses a scene resource. `resource.GetDefaultScene()`
instantiates its default scene. Those operations remain CPU-side; the shared render store resolves
referenced meshes and images when the scene provider builds its rendering passes.

The RenderFeatures example uses this flow to display DamagedHelmet in its top-left
quadrant, with three ordinary scene light entities. Other quadrants remain empty.

## Outputs and multiple views

`IRenderOutput::BuildTargets` returns fresh graph mappings for the current build.
Each logical view maps to an image, array layer, mip, viewport, format and encoding.
Outputs describe final destinations; providers can allocate intermediate HDR images
and auxiliary buffers. An `IRenderOutputResolver` must convert/composite intermediates
into those destinations and return the **new graph resource versions**. Without a
resolver, providers must already have produced the requested destination mappings.
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
