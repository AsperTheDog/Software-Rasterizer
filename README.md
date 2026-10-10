# Software-Rasterizer
CPU rasterizer made to be highly optimized and mimicking Vulkan's Pipeline Object + Command Buffer design.

# Overview
This is a CPU rasterizer I built to see how far I could take Vulkan's way of structuring a renderer without a GPU. Pipelines and command buffers are the core of it, and I wanted the code you write against it to look like the host side of a graphics API, instead of just a typical software renderer where you call a draw function and get pixels. I of course wanted it fast, so most of the work has gone into threading the pipeline stages and tuning the hot paths. Features were added whenever a technique I wanted to try (like shadow maps, deferred shading, bloom or instancing) needed something the API didn't have yet, and at each point I used the Vulkan equivalent as inspiration where it made sense. The list below shows what exists and what it maps to.

# Features
I left the Vulkan "equivalent" of each feature shown in parentheses where applicable.

- **Programmable pipelines**: Vertex input, uniform and vertex output are user types, validated by concepts (graphics pipeline with vertex and fragment shader modules)
- **Command buffer**: Recording scopes batch draws per pipeline and are committed into a command buffer that the renderer executes (`VkCommandBuffer`, `vkCmdBindPipeline`, `vkQueueSubmit`)
- **Draws**: Indexed and non-indexed, each with the uniform bound at record time (`vkCmdDraw`, `vkCmdDrawIndexed`, uniform buffers or push constants)
- **Instancing**: A pipeline may declare an instance input, shared uniform plus one instance buffer per draw, readable by both vertex and fragment shader (`instanceCount`, `VK_VERTEX_INPUT_RATE_INSTANCE`)
- **Multiple render targets**: The fragment shader returns a struct of `vec4`, one per target, and the pipeline declares the target formats (several color attachments, `VkRenderingInfo`)
- **Attachment formats**: Any glm format, sRGB encoding handled internally, float targets are stored unclamped (`VkFormat` such as `R8G8B8A8_SRGB` or `R32G32B32A32_SFLOAT`)
- **Depth testing**: Depth buffer per batch, test, write and compare operation are pipeline state (`VkPipelineDepthStencilStateCreateInfo`)
- **Face culling**: pipeline state (`VkPipelineRasterizationStateCreateInfo::cullMode`)
- **Depth-only pipelines**: A pipeline with no fragment shader and no targets, as used for shadow maps (pipeline without a fragment stage)
- **Discard**: The fragment shader may return an optional, nothing meaning discard (`discard` in a shader)
- **Blending**: Optional programmable blend shader applied to target 0 (`VkPipelineColorBlendAttachmentState`, which is fixed function there)
- **Clears**: Standalone clear of any texture, or clear-on-load state attached to a batch (`vkCmdClearColorImage`, `loadOp = CLEAR`)
- **Multipass**: Batches execute in order and may write any texture, so later passes sample earlier results
- **Render area**: A batch can override the extent, rendering to an offscreen target of a different size (`VkRenderingInfo::renderArea`)
- **Compute**: Compute pipelines dispatched with workgroups and local and global IDs (`vkCmdDispatch`)
- **Textures and samplers**: Morton swizzling, nearest, bilinear and trilinear filtering, clamp and repeat addressing, mipmaps (`VkImage`, `VkSampler`, `VkImageView`)
- **Fixed function rasterization**: Perspective-correct interpolation, top-left fill rule, near plane clipping and frustum culling

# Design
The idea I have always had when making it was that I wanted to try imitating modern graphics APIs. I really like the idea of command buffers and I think allowing the user to create a fully programmable pipeline was something worth pursuing. With those two things in mind I set myself to make the best possible system that allowed all of this, I then just kept iterating over it and adding features.

## Type safety
There was one thing I was sure I did was not willing to compromise in, and that was type safety and usability of the API. I wanted to try to ensure the users got a clear path towards getting a renderer working without having to do weird pointer casting or fiddle with unsafe pointers. I decided to hide type erasure behind a recording scope class. This proved to be a very useful system to batch together draw calls within a single pipeline, which allowed me to do huge optimizations that will be discussed later. Additionally, I decided to use Cpp 20 concepts to avoid generic template errors and enforce the correctness of a pipeline created by the user. I believe the system I ended up with is quite comfortable to use and allows shader code to be done with the custom types the user has created. It also ensures that command buffer recordings are given the correct types at all times, which is something I am very happy with.

## Command recording
I love command buffers in Vulkan, so I decided I definitely wanted to imitate them. There isn't really that much of an improvement when it comes to performance or ease of use since my rasterizer is not parallelizing nearly as aggressive as a real driver does, but I nonetheless wanted to do it. The most complex part was ensuring a single command buffer could store indefinite commands from arbitrary amounts of pipelines, this meant I either had to use polymorphism or type erasure. I decided to go for the second because I don't really want to force inheritance with all the problems it brings. Implementing type erasure simply meant using void* everywhere internally and storing strides. For interpolation flat types are not supported, which means every vertex output must be a float (or structs of floats). The flat alternative is per-instance data, which fragment shaders of instanced pipelines receive directly instead of through interpolation. I believe I ended up with something reasonably usable with minimal performance penalty.

## Concurrency
I was completely sure I wanted to make the system multithreaded. We are imitating a GPU after all, so that is kind of the whole point. I went with a persistent worker thread system that executes each phase in order while being synchronized by the main thread. Each recording scope is batched by pipelines. Since the code differs, I decided not to execute anything concurrently in between pipeline changes, which I think is fine, since the threads can easily get saturated with a single set of draw calls anyway. I do however batch draw calls within a single pipeline bind together.

### Vertex stage
This one is the most simple in terms of architecture, it simply gathers all the vertices to be processed from all draw calls in a batch and divides them evenly by thread. Each thread independently figures out the range it has to process by using its own ID and iterates through each triangle, executing the vertex shader function. An instanced draw is expanded when the batch is built, so each instance becomes a draw call of its own that shares the uniform and points at its slice of the instance data. This means nothing past this point knows about instancing, apart from each triangle remembering its instance pointer for the fragment shader.

### Binning stage
This is where things get interesting. Each thread will now take in a triangle and generate a bounding box for it. It will then decide in which tiles it belongs. A tile is simply a region of the screen that will be processed by a single thread later. In order to avoid contention when adding triangles to a tile, the system uses a simple set of linked lists within a giant preallocated array. Atomics are used to avoid race conditions while keeping the system lock-free. At this stage, triangles also get clipped/culled if necessary.

### Fragment stage
This stage is actually doing a lot of things at the same time. Each thread will take a list of tiles and start processing. The first step is to take in the linked list of triangles and move it to a new array, which will be sorted by triangleID. This is crucial to ensure that triangles are rendered in the same order their draw calls are issued, which is important for blending and transparency. After that, for each triangle, it creates a bounding box of the triangle once more, but clamps it to the current tile. Then, each pixel within the box is processed and, if deemed to be inside of the triangle it is processed (edges follow a top-left fill rule, so pixels shared by two triangles are never drawn twice). A processed pixel gets their depth interpolated and tested (if the pipeline configuration enabled it) against the depth buffer. If the depth test is passed then the vertex output is interpolated with perspective correction for that pixel and passed to the fragment shader, which is executed. The fragment shader returns one RGBA value per render target (or nothing, to discard the pixel). The value for the first target goes through the blend shader (if there is any) and every value is then stored in its texture, converted to that texture's format.

### Compute stage
A much simpler pipeline dispatch compared to the graphics pipeline, but it required decoupling the command buffer from draw calls to support compute calls as well. It works in a similar way to how the graphics pipeline works, but only has a compute stage. workgroup logic and predefined IDs present in GPUs is also here.

## Textures
Textures have many capabilities baked into them. One of the main ones is that, if dimensions allow it, the texture will be swizzled using morton order for better sampling performance. Additionally, they support sampler options like linear filtering and clamp/border options. Finally, the images can be formatted as sRGB, which will be handled internally. Textures can be of any format defined by glm (which includes most privimites via any variation of glm::vec1).
I have added MipTexture as well, which is supported via an extra optional "shader" in the Pipeline that simply asks for the UV data (because formats are type erased inside the renderer) so the amount of variation in UV coordinates in a triangle can be calculated relative to the amount of pixels it occupies on the screen. The fragment shader provides the texelsPerWorldUnit variable (tpw) which is then using when sampling to determine what mipmap level to use. Everything else is calculated internally. Trilinear filtering is supported as well.

## SIMD
Fragment shaders are plain scalar functions written by the user, so the rasterizer cannot shade several pixels at once. I tried letting the compiler vectorize the coverage and depth tests, but things got slower overall, since shading and writing dominate the cost of a pixel. What did matter was texture sampling, so that is where the effort went. First the data was made friendlier: Morton indices are expanded once per texel coordinate through a lookup table instead of once per texel, and sRGB encoding goes through lookup tables that write the final bytes directly. On top of that, bilinear and trilinear blending of 8 bit RGBA textures use SSE2, with trilinear keeping both mip levels in registers.

The intrinsics are controlled by the SIMD_INTRINSICS macro in [texture.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/texture.hpp). It defaults to 1 on x86-64 and to 0 everywhere else, and can be forced by defining it (for example with -DSIMD_INTRINSICS=0).

## Presentation
Two simple presentation engines have been made: One uses the terminal (it's painfully slow but it's as plug and play as it gets), the other is an SDL3 window (as fast as it gets, but needs SDL3 to work). The SDL3 window additionally allows you to control the camera, which lets you move and look round.

# Scenes
The render loop in [main.cpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/main.cpp) is a small host for scenes. A scene is any type satisfying the Scene concept in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp). It is constructed from a CommandBuffer, reports a name and a camera setup, and records its own draw calls into the buffer each frame. I made some scenes as examples with different properties, feel free to switch them around and play with them as you like.

The multipass scenes live in [multipass.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/multipass.hpp). ShadowScene renders a depth-only pass from the light and samples it in the lit pass, DeferredScene writes a two target G-buffer and shades it with 32 point lights in a fullscreen pass, and MonitorScene renders a small scene into a texture that is displayed on a screen inside a room of spinning cubes. NeonScene is a simple emissive scene, and BloomScene and PostFxScene wrap any other scene to add a bloom or a post-processing pass (chromatic aberration and vignette) on top.

Most example scenes have been created using AI.

# Benchmarks
All numbers are per frame at 1920x1080 rendering to an sRGB target and presenting through the SDL3 window, so the frame time covers recording the scene, clearing, rendering and presenting. They were measured on a Ryzen 7 5700X with MSVC 14.51 at /O2 (Release) on Windows 11. Each scene was launched 3 times, every launch averaging 30 samples of the frame timers after a warmup, and the table shows the median launch (the spread between launches was at most 3%, except for the Monitor scene at 7% since its frames are so short). Scenes animate as they normally would and the glTF models are rendered from a fixed camera, the same ones used in the images below. Triangles and draw batches are what the scene submits each frame, counting every pass. The stage columns are the renderer's own timers, so they do not add up to the frame time.

| Scene | Triangles | Draw batches | Vertex | Binning | Fragment | Frame | FPS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Helmet (PBR) | 15,452 | 1 | 0.60 ms | 0.15 ms | 10.98 ms | 13.41 ms | 74.6 |
| Sponza (PBR) | 262,267 | 2 | 2.47 ms | 1.02 ms | 61.32 ms | 67.24 ms | 14.9 |
| Bistro (PBR) | 2,828,266 | 2 | 27.98 ms | 7.48 ms | 71.79 ms | 108.49 ms | 9.2 |
| Hairball (PBR) | 2,850,000 | 1 | 8.41 ms | 10.16 ms | 58.17 ms | 78.56 ms | 12.7 |
| Cubes (instanced) | 1,500 | 1 | 0.49 ms | 0.12 ms | 13.80 ms | 16.04 ms | 62.3 |
| Torus knot | 9,216 | 1 | 0.54 ms | 0.12 ms | 5.32 ms | 7.52 ms | 133.0 |
| Terrain | 73,728 | 1 | 0.69 ms | 0.35 ms | 8.54 ms | 11.26 ms | 88.8 |
| Stress | 8,000 | 1 | 0.60 ms | 0.19 ms | 9.05 ms | 11.78 ms | 84.9 |
| Shadow | 18,554 | 2 | 0.99 ms | 0.25 ms | 15.56 ms | 18.36 ms | 54.5 |
| Deferred (32 lights) | 9,663 | 3 | 2.76 ms | 0.39 ms | 37.62 ms | 42.89 ms | 23.3 |
| Deferred + bloom + postfx | 9,668 | 8 | 3.35 ms | 0.96 ms | 85.71 ms | 92.05 ms | 10.9 |
| Monitor | 9,350 | 3 | 0.81 ms | 0.26 ms | 2.81 ms | 5.31 ms | 188.5 |
| Neon | 9,288 | 1 | 0.48 ms | 0.12 ms | 3.12 ms | 5.11 ms | 195.9 |

# Building
This is a very simple Cpp 20 project. The main two dependencies are the header only libraris [GLM](https://github.com/g-truc/glm) and [STB](https://github.com/nothings/stb) (specifically STB_Image). The .glb loader also needs [tinygltf](https://github.com/syoyo/tinygltf) v3, which is a header plus its .c file, so point TINYGLTF_INCLUDE_DIR at the folder holding tiny_gltf_v3.h, tiny_gltf_v3.c and tinygltf_json_c.h. Just tell CMake where they are when building. Optionally, you can also point to the location of SDL3 if you wish to use the SDL3 output. The project expects a path to the include files folder and a path to the .lib folder.

---

<img width="1922" height="1119" alt="cubes" src="https://github.com/user-attachments/assets/0c130b2d-e2f7-4ee2-9281-3d6ffd7919c1" />

Capture of the cubes scene from [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp). It uses a simple diffuse lighting pipeline and renders a cube 125 times with a single instanced draw, where the instance data holds each cube's model matrix and color. The cubes also have a texture which is sampled with mipmapping using trilinear filtering. The render is done to an SRGB image which is then shown through an SDL3 window.

Properties: 1920x1080 sRGB target shown through the SDL3 window, 1,500 triangles (125 instances) in 1 draw batch, trilinear mipmapped texture. Performance: 16.04 ms per frame (62.3 fps), 0.49 ms in the vertex stage, 0.12 ms in binning and 13.80 ms in the fragment stage.

<img width="2560" height="1380" alt="image" src="https://github.com/user-attachments/assets/1dae0e9a-209b-4b43-b021-e95eca43f6bf" />

Capture of the same render loop than the previous image, but the output is being shown through the terminal. SDL3 is completely optional and chosen in the CMake config. The screen tearing is inevitable because the terminal is so slow you see it sweeping its text to change frames.

---

<img width="1922" height="1119" alt="helmet" src="https://github.com/user-attachments/assets/6357dc73-d104-4c41-b64d-d2202049ddc3" />

Capture of a helmet GLB using PRB rendering, it uses the GLTF scene defined in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp).

Properties: DamagedHelmet.glb, 1920x1080 sRGB target, 15,452 triangles in 1 draw batch, camera at (1.8, 1.15, 2.7). Performance: 13.41 ms per frame (74.6 fps), 0.60 ms in the vertex stage, 0.15 ms in binning and 10.98 ms in the fragment stage.

<img width="1922" height="1119" alt="torus_knot" src="https://github.com/user-attachments/assets/59341ba0-825d-4170-a3dc-5cc715d35ff2" />

Captura of a Torus Knot created using the Torus Knot Scene, defined in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp)

Properties: 1920x1080 sRGB target, 9,216 triangles in 1 draw batch, camera at (0, 9, 24). Performance: 7.52 ms per frame (133.0 fps), 0.54 ms in the vertex stage, 0.12 ms in binning and 5.32 ms in the fragment stage.

<img width="1922" height="1119" alt="sponza" src="https://github.com/user-attachments/assets/934e8d4a-d9ba-475e-b983-4611def4ddb5" />

Capture of Sponza using PBR rendering, it uses the same pipeline and scene system as with the helmet.

Properties: sponza.glb, 1920x1080 sRGB target, 262,267 triangles in 2 draw batches, camera at (0.72, -0.25, -0.005) looking down the nave. Performance: 67.24 ms per frame (14.9 fps), 2.47 ms in the vertex stage, 1.02 ms in binning and 61.32 ms in the fragment stage.

<img width="1922" height="1119" alt="bistro" src="https://github.com/user-attachments/assets/142f5fc9-86b9-4fc0-b76d-767e3ae16dd7" />

Capture of Bistro using PBR rendering, it uses the same pipeline and scene system as with the helmet and Sponza.

Properties: bistro.glb, 1920x1080 sRGB target, 2,828,266 triangles in 2 draw batches, camera at (-0.55, -0.09, -0.09). Performance: 108.49 ms per frame (9.2 fps), 27.98 ms in the vertex stage, 7.48 ms in binning and 71.79 ms in the fragment stage.

<img width="1922" height="1119" alt="hairball" src="https://github.com/user-attachments/assets/695a14b4-eb0a-4e3b-bed1-628445f0d1b3" />

Capture of the hairball model using the same PBR pipeline and scene system, a stress test for binning and rasterization since it is made of millions of thin triangles.

Properties: hairball.glb, 1920x1080 sRGB target, 2,850,000 triangles in 1 draw batch, camera at (1.8, 1.15, 2.7). Performance: 78.56 ms per frame (12.7 fps), 8.41 ms in the vertex stage, 10.16 ms in binning and 58.17 ms in the fragment stage.

---

<img width="1922" height="1119" alt="deferred_bloom_postfx" src="https://github.com/user-attachments/assets/48e61cf6-5187-46ec-9c21-448aac08ac7e" />

Capture that mixes together a lot of techniques. This scene is rendered with many lights using deferred rendering (deferred scene from [multipass.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/multipass.hpp)), and has bloom and post processing on top (bloom scene and postFX scene, both in [multipass.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp))

Properties: 1920x1080 sRGB target, 9,668 triangles in 8 draw batches (G-buffer, 32 point lights, bloom and post processing passes), camera at (0, 9, 17). Performance: 92.05 ms per frame (10.9 fps), 3.35 ms in the vertex stage, 0.96 ms in binning and 85.71 ms in the fragment stage.

<img width="1922" height="1119" alt="shadow" src="https://github.com/user-attachments/assets/064e8c0a-9f13-4212-a09c-525ed225d04f" />

Capture of the shadow scene from [multipass.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/multipass.hpp), a depth-only pass from the light that is sampled in the lit pass.

Properties: 1920x1080 sRGB target, 18,554 triangles in 2 draw batches, camera at (0, 9, 17). Performance: 18.36 ms per frame (54.5 fps), 0.99 ms in the vertex stage, 0.25 ms in binning and 15.56 ms in the fragment stage.

<img width="1922" height="1119" alt="monitor" src="https://github.com/user-attachments/assets/434c9d31-d304-4a22-bacb-bb0166dbf589" />

Capture of the monitor scene from [multipass.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/multipass.hpp), a small scene rendered into a texture that is displayed on a screen inside a room of spinning cubes.

Properties: 1920x1080 sRGB target plus an offscreen render target, 9,350 triangles in 3 draw batches, camera at (0, 0, 9). Performance: 5.31 ms per frame (188.5 fps), 0.81 ms in the vertex stage, 0.26 ms in binning and 2.81 ms in the fragment stage.
