
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
		Texture<glm::u8vec4>* tex;
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

	static VOutput interpolationShader(const VOutput* v1, const VOutput* v2, const VOutput* v3, const glm::vec3 barycentrics, const Uniform* uni)
	{
		VOutput vOut{};
		vOut.position = ShaderUtils::interpolateLinear(v1->position, v2->position, v3->position, barycentrics);
		vOut.normal = ShaderUtils::interpolatePerspective(v1->normal, v2->normal, v3->normal, barycentrics, v1, v2, v3);
		vOut.uvCoords = ShaderUtils::interpolatePerspective(v1->uvCoords, v2->uvCoords, v3->uvCoords, barycentrics, v1, v2, v3);
		return vOut;
	}

	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni)
	{
		const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
		const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);
		const glm::vec4 tex = uni->tex->sample(vOut->uvCoords * 2.f);
		return { diffuse * glm::vec3{tex} * uni->color, 1.0f };
	}

	static glm::vec4 blendShader(const glm::vec4& src, const glm::vec4& dst, const Uniform* uni)
	{
		const float srcAlpha = src.a;
		const float invSrcAlpha = 1.0f - srcAlpha;

		const glm::vec3 blendedRGB = glm::vec3(src) * srcAlpha + glm::vec3(dst) * invSrcAlpha;

		const float blendedAlpha = srcAlpha + dst.a * invSrcAlpha;

		return {blendedRGB, blendedAlpha};
	}
};

int main() 
{
	SdlWindow canvas{ 1920, 1080 };
	Renderer renderer{};

	Texture<glm::vec1> depthTexture{ canvas.getSize(), glm::vec1(std::numeric_limits<float>::infinity()) };
	Texture<glm::u8vec4> colorTexture{ canvas.getSize(), glm::u8vec4(0, 0, 0, 255), false };
	renderer.setFramesize(canvas.getSize());

	CommandBuffer commandBuffer{};

	Texture<glm::u8vec4> tex{ "../texture.png" };
	tex.setSampler(LINEAR, REPEAT);

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

	CommandBufferRecording<ColorPipeline> recording{ state };
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
	canvas.toggleMouseCaptured();
	cam.setMouseCaptured(true);

	while (canvas.isOpen())
	{
		//canvas.processInput(cam);
		//cam.updateEvents(renderer.getFrameTime() / 1000.f);

		for (int z = 0; z <= 4; z++)
		{
			for (int x = -2; x <= 2; x++)
			{
				for (int y = -2; y <= 2; y++)
				{
					glm::mat4 modelMat = glm::translate(glm::vec3(static_cast<float>(x) * 3.f, static_cast<float>(y) * 3.f, -4.f - static_cast<float>(z) * 3.f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 30.0f), glm::vec3(1.0f, 0.0f, 0.0f));

					glm::mat4 normalMat = glm::transpose(glm::inverse(modelMat));

					uniformData.modelViewProjectionMatrix = cam.getVPMatrix() * modelMat;
					uniformData.normalMatrix = normalMat;
					//uniformData.color = glm::vec3(static_cast<float>(x + 2) / 5.0f, static_cast<float>(y + 2) / 5.0f, 1 - (static_cast<float>(z) / 5.0f));
					uniformData.color = glm::vec3(1.0f, 1.0f, 1.0f);

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

		time += renderer.getFrameTime() / 1000.0f;
	}

    return 0;
}
