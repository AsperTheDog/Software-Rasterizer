# Software-Rasterizer
CPU rasterizer made to be highly optimized and mimicking Vulkan's Pipeline Object + Command Buffer design.

# Capabilities
This rasterizer is made with full rendering flexibility in mind. It supports fully programmable pipelines with custom vertex input, uniform, and vertex output structure data. It additionally supports depth buffer testing (programmable within the pipeline) and an optional custom blending shader stage for transparency. A fragment shader can also return an optional colour instead of a plain one (returning nothing is equivalent to discarding that pixel). If a pipeline does not need to discard anything, it can then simply define the fragment shader without an optional and the compiler will avoid the overhead statically. Rendering is done to a previously allocated texture, which means multipass rendering is not only possible but as easy to do as in a real graphics API. Clearing is also done through the Command Buffer. You can do it either with a standalone clear targeting any texture, or clear-on-load state attached to a draw call batch, in both cases interpreted in the format of whichever texture is being written to.

# Design
The idea I have always had when making it was that I wanted to try imitating modern graphics APIs. I really like the idea of command buffers and I think allowing the user to create a fully programmable pipeline was something worth pursuing. With those two things in mind I set myself to make the best possible system that allowed all of this.

## Type safety
There was one thing I was sure I did was not willing to compromise in, and that was type safety and usability of the API. I wanted to try to ensure the users got a clear path towards getting a renderer working without having to do weird pointer casting or fiddle with unsafe pointers. I decided to hide type erasure behind a recording scope class. This proved to be a very useful system to batch together draw calls within a single pipeline, which allowed me to do huge optimizations that will be discussed later. Additionally, I decided to use Cpp 20 concepts to avoid generic template errors and enforce the correctness of a pipeline created by the user. I believe the system I ended up with is quite comfortable to use and allows shader code to be done with the custom types the user has created. It also ensures that command buffer recordings are given the correct types at all times, which is something I am very happy with.

## Command recording
I love command buffers in Vulkan, so I decided I definitely wanted to imitate them. There isn't really that much of an improvement when it comes to performance or ease of use since my rasterizer is not parallelizing nearly as aggressive as a real driver does, but I nonetheless wanted to do it. The most complex part was ensuring a single command buffer could store indefinite commands from arbitrary amounts of pipelines, this meant I either had to use polymorphism or type erasure. I decided to go for the second because I don't really want to force inheritance with all the problems it brings. Implementing type erasure simply meant using void* everywhere internally and storing strides. For interpolation flat types are not supported, which means every type must be a float (or structs of floats). I believe I ended up with something reasonably usable with minimal performance penalty.

## Concurrency
I was completely sure I wanted to make the system multithreaded. We are imitating a GPU after all, so that is kind of the whole point. I went with a persistent worker thread system that executes each phase in order while being synchronized by the main thread. Each recording scope is batched by pipelines. Since the code differs, I decided not to execute anything concurrently in between pipeline changes, which I think is fine, since the threads can easily get saturated with a single set of draw calls anyway. I do however batch draw calls within a single pipeline bind together.

### Vertex stage
This one is the most simple in terms of architecture, it simply gathers all the vertices to be processed from all draw calls in a batch and divides them evenly by thread. Each thread independently figures out the range it has to process by using its own ID and iterates through each triangle, executing the vertex shader function.

### Binning stage
This is where things get interesting. Each thread will now take in a triangle and generate a bounding box for it. It will then decide in which tiles it belongs. A tile is simply a region of the screen that will be processed by a single thread later. In order to avoid contention when adding triangles to a tile, the system uses a simple set of linked lists within a giant preallocated array. Atomics are used to avoid race conditions while keeping the system lock-free. At this stage, triangles also get clipped/culled if necessary.

### Fragment stage
This stage is actually doing a lot of things at the same time. Each thread will take a list of tiles and start processing. The first step is to take in the linked list of triangles and move it to a new array, which will be sorted by triangleID. This is crucial to ensure that triangles are rendered in the same order their draw calls are issued, which is important for blending and transparency. After that, for each triangle, it creates a bounding box of the triangle once more, but clamps it to the current tile. Then, each pixel within the box is processed and, if deemed to be inside of the triangle it is processed. A processed pixel gets their depth interpolated and tested (if the pipeline configuration enabled it) against the depth buffer. If the depth test is passed then the interpolation shader is called, which returns a fully interpolated vertex output structure. This structure is then passed to the fragment shader, which is executed. The fragment shader returns an RGBA value that is passed to the blend shader (if there is any) and stored in the final image.

### Compute stage
A much simpler pipeline dispatch compared to the graphics pipeline, but it required decoupling the command buffer from draw calls to support compute calls as well. It works in a similar way to how the graphics pipeline works, but only has a compute stage. workgroup logic and predefined IDs present in GPUs is also here.

## Textures
Textures have many capabilities baked into them. One of the main ones is that, if dimensions allow it, the texture will be swizzled using morton order for better sampling performance. Additionally, they support sampler options like linear filtering and clamp/border options. Finally, the images can be formatted as SRGB, which will be handled internally. Textures can be of any format defined by glm (which includes most privimites via any variation of glm::vec1).
I have added MipTexture as well, which is supported via an extra optional "shader" in the Pipeline that simply asks for the UV data (because formats are type erased inside the renderer) so the amount of variation in UV coordinates in a triangle can be calculated relative to the amount of pixels it occupies on the screen. The fragment shader provides the texelsPerWorldUnit variable (tpw) which is then using when sampling to determine what mipmap level to use. Everything else is calculated internally. Trilinear filtering is supported as well.

## Presentation
Two simple presentation engines have been made: One uses the terminal (it's painfully slow but it's as plug and play as it gets), the other is an SDL3 window (as fast as it gets, but needs SDL3 to work). The SDL3 window additionally allows you to control the camera, which lets you move and look round.

# Scenes
The render loop in [main.cpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/main.cpp) is a small host for scenes. A scene is any type satisfying the Scene concept in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp). It is constructed from a CommandBuffer, reports a name and a camera setup, and records its own draw calls into the buffer each frame. I made some scenes as examples with different properties, feel free to switch them around and play with them as you like.

# Benchmarks
All numbers are the full frame time at 1280x720 rendering to an SRGB target, measured on a Ryzen 7 5700X with GCC 13 at -O2 under WSL2. Each value is the result of 5 runs of 8 frames after a warmup. Multithreaded runs use all 16 hardware threads and show the best run, since the OS noise at a few milliseconds is bigger than the differences otherwise. Single core runs show the median.

## All threads
| Scene | SIMD off | SIMD on | Speedup |
| --- | ---: | ---: | ---: |
| Cubes | 12.2 ms | 9.8 ms | 1.24x |
| Terrain | 6.8 ms | 7.1 ms | 0.96x |
| Torus knot | 4.5 ms | 4.1 ms | 1.09x |
| Blend | 4.4 ms | 4.0 ms | 1.09x |
| Mip debug | 1.9 ms | 1.8 ms | 1.04x |
| Stress | 6.1 ms | 5.9 ms | 1.03x |
| Compute | 4.5 ms | 4.6 ms | 0.99x |
| GLTF | 6.2 ms | 6.0 ms | 1.04x |

## Single core
| Scene | SIMD off | SIMD on | Speedup |
| --- | ---: | ---: | ---: |
| Cubes | 75.1 ms | 54.9 ms | 1.37x |
| Terrain | 39.5 ms | 38.3 ms | 1.03x |
| Torus knot | 32.4 ms | 25.1 ms | 1.29x |
| Blend | 20.3 ms | 15.7 ms | 1.29x |
| Mip debug | 4.1 ms | 3.3 ms | 1.24x |
| Stress | 32.9 ms | 32.1 ms | 1.02x |
| Compute | 23.0 ms | 22.1 ms | 1.04x |
| GLTF | 49.6 ms | 46.5 ms | 1.07x |

## Texture filtering
The filter is the only thing that changes between rows, scenes that do not sample a texture are not affected by it. Measured with all threads.

| Scene | Filter | SIMD off | SIMD on |
| --- | --- | ---: | ---: |
| Cubes | Nearest | 8.4 ms | 8.2 ms |
| Cubes | Bilinear | 10.1 ms | 9.0 ms |
| Cubes | Trilinear | 12.2 ms | 9.8 ms |
| Torus knot | Nearest | 3.8 ms | 3.9 ms |
| Torus knot | Bilinear | 4.1 ms | 3.7 ms |
| Torus knot | Trilinear | 4.5 ms | 4.1 ms |

I have found that mostly it is when using trilinear when the intrinsics really matter, because it blends two bilinear samples and the compiler could not keep the intermediate colors in registers by itself (thus no automatic intrinsics were used). In the cubes scene trilinear costs about 21% more than bilinear without them, and around 9% more with them. Nearest filtering never touches the blend code, so the small differences between its two columns are just noise (I still left them as reference and for completeness).

# Building
This is a very simple Cpp 20 project. The main two dependencies are the header only libraris [GLM](https://github.com/g-truc/glm) and [STB](https://github.com/nothings/stb) (specifically STB_Image). The .glb loader also needs [tinygltf](https://github.com/syoyo/tinygltf) v3, which is a header plus its .c file, so point TINYGLTF_INCLUDE_DIR at the folder holding tiny_gltf_v3.h, tiny_gltf_v3.c and tinygltf_json_c.h. Just tell CMake where they are when building. Optionally, you can also point to the location of SDL3 if you wish to use the SDL3 output. The project expects a path to the include files folder and a path to the .lib folder.

---

<img width="1922" height="1119" alt="image" src="https://github.com/user-attachments/assets/74a5a187-c1a7-4351-88de-289cfcffb853" />

Capture of the cubes scene from [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp). It uses a simple diffuse lighting pipeline and renders a cube 125 times with slightly different model matrices and colors defined in the uniforms, they also have a texture which is sampled with mipmapping using trilinear filtering. The render is done to an SRGB image which is then shown through an SDL3 window.

<img width="2560" height="1380" alt="image" src="https://github.com/user-attachments/assets/1dae0e9a-209b-4b43-b021-e95eca43f6bf" />

Capture of the same render loop than the previous image, but the output is being shown through the terminal. SDL3 is completely optional and chosen in the CMake config. The screen tearing is inevitable because the terminal is so slow you see it sweeping its text to change frames.

---

<img width="1922" height="1119" alt="image" src="https://github.com/user-attachments/assets/31cc26da-f83d-4659-8697-23e60cc71d24" />

Capture of a helmet GLB using PRB rendering, it uses the GLTF scene defined in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp).

<img width="1922" height="1119" alt="image" src="https://github.com/user-attachments/assets/58a0b7fe-7bbb-49af-bd30-cec2be762ce5" />

Captura of a Torus Knot created using the Torus Knot Scene, defined in [scenes.hpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/scenes.hpp)

<img width="1922" height="1119" alt="image" src="https://github.com/user-attachments/assets/0d42e248-f17d-4b6e-84e6-c5103fea4b34" />

Capture of Sponza using PBR rendering, it uses the same pipeline and scene system as with the helmet.