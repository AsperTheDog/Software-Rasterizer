# Software-Rasterizer
CPU rasterizer made to be highly optimized and mimicking Vulkan's Pipeline Object + Command Buffer design.

# Capabilities
This rasterizer is made with full rendering flexibility in mind. It supports fully programmable pipelines with custom vertex input, uniform, and vertex output structure data. It additionally supports depth buffer testing (programmable within the pipeline) and an optional custom blending shader stage for transparency. Draw calls can receive either only a vertex buffer, or a vertex + index buffer combo. Rendering is done to a previously allocated texture, which means multipass rendering is not only possible but as easy to do as in a real graphics API.

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

# Building
This is a very simple Cpp 20 project. The main two dependencies are the header only libraris [GLM](https://github.com/g-truc/glm) and [STB](https://github.com/nothings/stb) (specifically STB_Image). Just tell CMake where it is when building. Optionally, you can also point to the location of SDL3 if you wish to use the SDL3 output. The project expects a path to the include files folder and a path to the .lib folder.

---

<img width="1922" height="1119" alt="image" src="https://github.com/user-attachments/assets/74a5a187-c1a7-4351-88de-289cfcffb853" />

Capture of the render loop created in [main.cpp](https://github.com/AsperTheDog/Software-Rasterizer/blob/main/src/main.cpp). It uses a simple diffuse lighting pipeline and renders a cube 125 times with slightly different model matrices and colors defined in the uniforms, they also have a texture which is sampled with mipmapping using trilinear filtering. The render is done to an SRGB image which is then shown through an SDL3 window.

<img width="2056" height="1160" alt="image" src="https://github.com/user-attachments/assets/75dc9233-5f53-47a0-9e29-091fa3e190a4" />

Capture of the same render loop than the previous image, but the output is being shown through the terminal. SDL3 is completely optional and chosen in the CMake config.
