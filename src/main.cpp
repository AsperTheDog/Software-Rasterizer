
#include <glm.hpp>

#include "command_buffer.hpp"
#include "pipeline.hpp"
#include "present.hpp"
#include "renderer.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>

#include "Texture.hpp"

class ColorPipeline
{
public:
	struct UniformStruct
	{
		MipTexture<glm::u8vec4>* tex;
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 normalMatrix;
		glm::vec3 lightDirection;
		glm::vec3 color;
		float ambientMult;
	};

	struct VIn
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uvcoords;
	};

	struct VOut : VOutBase
	{
		glm::vec3 normal;
		glm::vec2 uvCoords;
	};

	typedef UniformStruct Uniform;
	typedef VIn VInput;
	typedef VOut VOutput;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni)
	{
		VOutput vOut{};
		vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
		vOut.normal = uni->normalMatrix * glm::vec4(vIn->normal, 0.0f);
		vOut.uvCoords = vIn->uvcoords;
		return vOut;
	}

	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
	{
		const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
		const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);
		const glm::vec4 tex = uni->tex->sample(vOut->uvCoords, tpw, 2.f);
		return { diffuse * glm::vec3{tex} * uni->color, 1.0f };
	}

	static glm::vec2 getUV(const VOutput* vOut)
	{
		return vOut->uvCoords;
	}

	/*static glm::vec4 blendShader(const glm::vec4& src, const glm::vec4& dst, const Uniform* uni)
	{
		const float srcAlpha = src.a;
		const float invSrcAlpha = 1.0f - srcAlpha;

		const glm::vec3 blendedRGB = glm::vec3(src) * srcAlpha + glm::vec3(dst) * invSrcAlpha;

		const float blendedAlpha = srcAlpha + dst.a * invSrcAlpha;

		return {blendedRGB, blendedAlpha};
	}*/
};

int main() 
{
#ifdef SDL_OUTPUT
	SdlWindow canvas{ 1920, 1080 };
#else
	TerminalCanvas canvas{ 1000, 500 };
#endif
	Renderer renderer{};

	renderer.setFramesize(canvas.getSize(), 1);

	Texture<glm::vec1> depthTexture{ renderer.getFramesize(), glm::vec1(std::numeric_limits<float>::infinity()) };
	Texture<glm::u8vec4> colorTexture{ canvas.getSize(), glm::u8vec4(0, 0, 0, 255), false };
	colorTexture.setFormat(SRGB);

	CommandBuffer commandBuffer{};

	MipTexture<glm::u8vec4> tex{ "../texture.png" };
	tex.setSampler(TRILINEAR, REPEAT);
	//tex.toggleDebugMode();

	ColorPipeline::Uniform uniformData{
		.tex = &tex,
		.modelViewProjectionMatrix = {},
		.normalMatrix = {},
		.lightDirection = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f)),
		.color = {},
		.ambientMult = 0.1f
	};

	PipelineState state{
		.depthTest = true,
		.depthWrite = true,
		.depthOp = PipelineState::DepthOp::Less,
		.cullMode = PipelineState::CullMode::Back,
		.outputFormatSize = 0,
		.outputFormatNorm = 0
	};
	state.setFormat<glm::u8vec4>();

	PipelineID ppid = commandBuffer.registerPipeline<ColorPipeline, glm::u8vec4>(state);
	CommandBufferRecording<ColorPipeline> recording{ ppid };
	recording.reserve(5 * 5 * 5, 5 * 5 * 5);

	constexpr ColorPipeline::VIn vertices[] = {
		// Front face (+Z)
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {0.0f, 1.0f} },

		// Back face (-Z)
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {0.0f, 1.0f} },

		// Left face (-X)
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		// Right face (+X)
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		// Top face (+Y)
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		// Bottom face (-Y)
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} }
	};

	constexpr uint32_t indices[] = {
		0, 1, 2, 2, 3, 0,       // Front face
		4, 5, 6, 6, 7, 4,       // Back face
		8, 9, 10, 10, 11, 8,    // Left face
		12, 13, 14, 14, 15, 12, // Right face
		16, 17, 18, 18, 19, 16, // Top face
		20, 21, 22, 22, 23, 20  // Bottom face 
	};

	constexpr uint32_t vertNum = sizeof(vertices) / sizeof(ColorPipeline::VIn);
	constexpr uint32_t indNum = sizeof(indices) / sizeof(uint32_t);

	float time = 0.0f;

	Camera cam{ { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }};
	cam.setScreenSize(canvas.getSize().x, canvas.getSize().y);
#ifdef SDL_OUTPUT
	canvas.toggleMouseCaptured();
	cam.setMouseCaptured(true);
#else
	initializeTerminal();
#endif

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

		for (int z = 0; z <= 4; z++)
		{
			for (int x = -2; x <= 2; x++)
			{
				for (int y = -2; y <= 2; y++)
				{
					glm::mat4 modelMat = glm::translate(glm::vec3(static_cast<float>(x) * 3.f, static_cast<float>(y) * 3.f, -4.f - static_cast<float>(z) * 3.f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 5.f), glm::vec3(0.0f, 1.0f, 0.0f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 3.f), glm::vec3(1.0f, 0.0f, 0.0f));

					glm::mat4 normalMat = glm::transpose(glm::inverse(modelMat));

					uniformData.modelViewProjectionMatrix = cam.getVPMatrix() * modelMat;
					uniformData.normalMatrix = normalMat;
					uniformData.color = glm::vec3(static_cast<float>(x + 2) / 5.0f, static_cast<float>(y + 2) / 5.0f, 1 - (static_cast<float>(z) / 5.0f));
					
					recording.bindUniform(uniformData);
					recording.drawIndexed({ vertices, vertNum }, { indices, indNum });
				}
			}
		}

		recording.commit(commandBuffer, colorTexture, &depthTexture);
		renderer.execute(commandBuffer);

		commandBuffer.clear();
		recording.clear();
		
		PerformanceData perfData;
		perfData.vertexTime = renderer.getVertexTime();
		perfData.binningTime = renderer.getBinningTime();
		perfData.fragmentTime = renderer.getFragmentTime();
		perfData.frameTime = renderer.getFrameTime();

		canvas.present(colorTexture, perfData, cam);
		colorTexture.clear(glm::u8vec4(0, 0, 0, 255));
		depthTexture.clear(glm::vec1(std::numeric_limits<float>::infinity()));

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
