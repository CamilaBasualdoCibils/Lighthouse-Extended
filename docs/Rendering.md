# Renderer infrastructure

The Extended renderer coordinates independently configured `RenderFeatureView`s.
Each view owns a render feature, a logical view family, and an `IRenderOutput`.
`RenderGraphComposer` and its `IOutputView`s continue to acquire and present images.
There is no renderer-wide draw batch.

This implementation provides interfaces, typed provider ownership, update calls,
graph assembly, destination mapping, and configuration lifetime management.
Deferred/forward shading, light assignment, reflection/shadow algorithms,
ray dispatch, denoising, tone mapping and output conversion are **not implemented**.
The existing resource system and `ResourceRequests` stubs are unchanged.

## One view

```cpp
auto rendererRef = composer.AddTechnique<Renderer>(&composer);
auto& renderer = *composer.GetTechnique<Renderer>(rendererRef);
auto sceneData = std::make_shared<SceneRenderData>(scene);
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

Scene adapters can share `SceneRenderData`, retaining scene change tracking across
views and feature changes. Each provider owns its batching/render implementation;
custom providers can own different data layouts, resources and command recording.
`IRenderPreparationProvider` is for generation/residency passes needed before the
feature builds its geometry. Providers add custom passes through the supplied
graph, with explicit reads/writes. Composite providers can read depth, GBuffer or
lit color as appropriate. Transparency ordering across providers is the responsibility
of their composition implementation.

Scene raster adapters currently extract data but add no drawing passes. The ray
adapter contributes instance descriptions only; it builds no acceleration structures.

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
