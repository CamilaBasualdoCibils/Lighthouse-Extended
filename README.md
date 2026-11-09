# Lighthouse-Extended

Reusable scenes, entities, transforms, cameras, materials, lighting, post-processing, and scene rendering. CPU assets, mesh/image data and glTF loading come from Engine. Public types use namespace `Lighthouse`.

## Build and consume

Clone recursively. Configure with the `vcpkg-preset` preset, then build and run CTest. The repository manifest declares dependencies for Extended and its nested Engine dependency; its overlay configuration includes Engine's patched ImGui port.

```sh
cmake --preset vcpkg-preset -DLIGHTHOUSE_EXTENDED_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

An unrelated application can use:

```cmake
add_subdirectory(extern/Lighthouse-Extended)
target_link_libraries(MyApplication PRIVATE Lighthouse::Extended)
```

Engine is initialized automatically unless `Lighthouse::Engine` already exists. `LIGHTHOUSE_EXTENDED_BUILD_TESTING` controls Extended's tests; `BUILD_TESTING` controls Engine's tests. Existing backend and ImGui selection options remain available. GPU integration tests require both render backends; scene/entity and asset tests do not.

Include `<Lighthouse/Extended/World/Scene.hpp>` for entities and scenes, and `<Lighthouse/Extended/Rendering/Renderer.hpp>` for the provided scene renderer. A Scene does not require constructing a Renderer.

## Rendering

Engine's `RenderGraphComposer` owns the device, views, techniques, compiled graph, and GPU execution. Extended's `Renderer` is an `IRenderTechnique` that builds the existing per-camera geometry, lighting, post-processing, and presentation passes. Register it with the composer using `AddTechnique<Renderer>(&composer, &scene, window)`.

`Renderer` renders entities with `CameraView` and `Transform` components by default. Applications with their own camera-owner components may supply a camera provider returning stable component references. Adding/removing cameras or extensions takes effect when the composer compiles the graph again, like the existing technique configuration.

`AddExtension` takes ownership of an `IRenderExtension`. Extensions run in registration order at `BeforeGeometry`, `Geometry`, `AfterGeometry`, `BeforeLighting`, `AfterLighting`, and `BeforePresentation`, once per camera. `RenderContext` supplies the technique/device, scene, camera entity/view, view-projection, mutable G-buffer/color handles, delta time, and graph-build ID. Context references are valid during graph construction; execution callbacks must capture the data they need. G-buffer handles are available from Geometry onward; color is available after lighting. Update context handles when adding resource versions so subsequent passes consume them.

## Embedded resources

Every file under `res/` is generated into the separate static target `Lighthouse::ExtendedEmbedded`, linked through `Lighthouse::Extended`. Resource changes, additions, and removals regenerate the library during a build. There is no runtime dependency on `res/`.

```cpp
#include <Lighthouse/Extended/EmbeddedResources.hpp>
auto text = Lighthouse::ExtendedEmbedded::ReadText("materials/Lit.glsl");
auto bytes = Lighthouse::ExtendedEmbedded::Find("materials/Lit.glsl");
```

`Find` returns an optional immutable byte span with executable lifetime, retaining embedded NULs and empty files; missing paths return `nullopt`. `ReadText` throws for missing paths. Paths use forward slashes relative to `res/`.

Built-in material/model/pass shaders use these embedded resources. The default `Materials(device)` resolves embedded material paths. `Materials(device, root)` loads application-owned material definitions and shader files from the supplied root, while still obtaining built-in shading infrastructure from the embedded library. The `EESG_` GLSL implementation symbols are retained to preserve the existing shader contract; they are not dependencies on the game repository.

## Pending Git registration

This repository was initially empty. No commits or pushes have been made. Its Engine gitlink records the preserved `b9fddd28c5584bac0142ec705b0e09a5d44e7b10` revision and the migration changes remain uncommitted in Engine.

EESG cannot record an Extended gitlink until Extended has its first commit. Publishing reproducible recursive clones will require authorized Engine changes, an Extended commit recording that new Engine revision, and an EESG commit recording Extended. None is created by this migration.
