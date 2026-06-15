
#include <glm.hpp>

#include "command_buffer.hpp"
#include "pipeline.hpp"
#include "present.hpp"
#include "renderer.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>

class ColorPipeline
{
public:
	struct VIn
	{
		glm::vec3 position;
		glm::vec3 normal;
	};

	struct Vout : VOutBase
	{
		glm::vec3 normal;
	};

	struct Uniform
	{
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 normalMatrix;
		glm::vec3 lightDirection;
		glm::vec3 color;
		float ambientMult;
	};

	static Vout vertexShader(const VIn* vIn, const Uniform* uni)
	{
		Vout vOut{};
		vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
		vOut.normal = uni->normalMatrix * glm::vec4(vIn->normal, 0.0f);
		return vOut;
	}

	static Vout interpolationShader(const Vout* v1, const Vout* v2, const Vout* v3, const glm::vec3 barycentrics, const Uniform* uni)
	{
		Vout vOut{};
		vOut.position = ShaderUtils::interpolateLinear(v1->position, v2->position, v3->position, barycentrics);
		vOut.normal = ShaderUtils::interpolatePerspective(v1->normal, v2->normal, v3->normal, barycentrics, v1, v2, v3);
		return vOut;
	}

	static glm::vec4 fragmentShader(const Vout* vOut, const Uniform* uni)
	{
		const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
		const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);
		return {glm::vec3(diffuse) * uni->color, 1.0f};
	}
};

int main() 
{
	TerminalCanvas canvas{ 300, 150 };
	Renderer renderer{};
	renderer.attachOutput(canvas);

	CommandBuffer commandBuffer{};

	glm::vec3 camPos{ 0.0f, 0.0f, 5.0f };
	glm::vec3 camDir{ 0.0f, 0.0f, -1.0f };

	glm::mat4 viewMat = glm::lookAt(camPos, camPos + camDir, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 projMat = glm::perspective(glm::radians(70.0f), 800.0f / 600.0f, 0.1f, 100.0f);

	ColorPipeline::Uniform uniformData{
		.lightDirection = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f)),
		.ambientMult = 0.1f
	};

	constexpr PipelineState state{ .depthTest = true, .depthWrite = true };
	CommandBufferRecording<ColorPipeline, ColorPipeline::Uniform, ColorPipeline::VIn, ColorPipeline::Vout> recording{ state };
	recording.reserve(5 * 5 * 5, 5 * 5 * 5);

	constexpr ColorPipeline::VIn vertices[] = {
		// Front face (+Z)
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f} },

		// Back face (-Z)
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f} },

		// Left face (-X)
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f} },

		// Right face (+X)
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f} },

		// Top face (+Y)
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f} },

		// Bottom face (-Y)
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f} }
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

	initializeTerminal();

	while (true)
	{
		for (int x = -2; x <= 2; x++)
		{
			for (int y = -2; y <= 2; y++)
			{
				for (int z = 0; z <= 4; z++)
				{
					glm::mat4 modelMat = glm::translate(glm::vec3(static_cast<float>(x) * 3.f, static_cast<float>(y) * 3.f, -4.f - static_cast<float>(z) * 3.f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
					modelMat = glm::rotate(modelMat, glm::radians(time * 30.0f), glm::vec3(1.0f, 0.0f, 0.0f));

					glm::mat4 normalMat = glm::transpose(glm::inverse(modelMat));

					uniformData.modelViewProjectionMatrix = projMat * viewMat * modelMat;
					uniformData.normalMatrix = normalMat;
					uniformData.color = glm::vec3(static_cast<float>(x + 2) / 5.0f, static_cast<float>(y + 2) / 5.0f, static_cast<float>(z) / 5.0f);

					recording.bindUniform(uniformData);
					recording.drawIndexed({ vertices, vertNum }, { indices, indNum });
				}
			}
		}

		recording.commit(commandBuffer);
		renderer.execute(commandBuffer);

		commandBuffer.clear();
		recording.clear();
		
		canvas.present();
		canvas.clear();

		time += 0.032f;
	}

	restoreTerminal();
    return 0;
}
