#include <glm.hpp>

#include "command_buffer.hpp"
#include "pipeline.hpp"
#include "present.hpp"
#include "renderer.hpp"
#include "scenes.hpp"

#include <limits>

constexpr uint32_t kDownsample = 1;

template<Scene S>
static int runApp()
{
#ifdef SDL_OUTPUT
	SdlWindow canvas{ 1920, 1080 };
#else
	TerminalCanvas canvas{ 1000, 500 };
#endif
	Renderer renderer{};

	renderer.setFramesize(canvas.getSize(), kDownsample);

	Texture<glm::vec1> depthTexture{ renderer.getFramesize(), glm::vec1(std::numeric_limits<float>::infinity()) };
	Texture<glm::u8vec4> colorTexture{ canvas.getSize(), glm::u8vec4(0, 0, 0, 255), false };
	colorTexture.setFormat(SRGB);

	CommandBuffer commandBuffer{};
	S scene{ commandBuffer };

	const CameraSetup cameraSetup = S::cameraSetup();
	Camera cam{ cameraSetup.position, cameraSetup.direction, cameraSetup.fov };
	cam.setScreenSize(canvas.getSize().x, canvas.getSize().y);

#ifdef SDL_OUTPUT
	canvas.toggleMouseCaptured();
	cam.setMouseCaptured(true);
#else
	initializeTerminal();
#endif

	float time = 0.0f;

#ifdef SDL_OUTPUT
	while (canvas.isOpen())
#else
	while (true)
#endif
	{
#ifdef SDL_OUTPUT
		canvas.processInput(cam);
		cam.updateEvents(renderer.getFrameTime() / 1000.f);
#endif
		scene.record(commandBuffer, colorTexture, depthTexture, cam, time);
		renderer.execute(commandBuffer);

		commandBuffer.clear();

		PerformanceData perfData;
		perfData.sceneName = S::name();
		perfData.vertexTime = renderer.getVertexTime();
		perfData.binningTime = renderer.getBinningTime();
		perfData.fragmentTime = renderer.getFragmentTime();
		perfData.clearTime = renderer.getClearTime();
		perfData.frameTime = renderer.getFrameTime();
		perfData.binningOverflow = renderer.getBinningOverflowCount();
		perfData.clipOverflow = renderer.getClipOverflowCount();

		canvas.present(colorTexture, perfData, cam);

		time += renderer.getFrameTime() / 100.f;
		if (time > 360.0f)
			time -= 360.0f;
		renderer.endFrame();
	}

#ifndef SDL_OUTPUT
	restoreTerminal();
#endif

	return 0;
}

int main()
{
	return runApp<GltfScene>();
}
