#include "scenes.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

const glm::vec3 kLightDirection = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f));

namespace
{
	PipelineState colorState(const PipelineState::CullMode cullMode, const bool depthWrite)
	{
		PipelineState state{
			.depthTest = true,
			.depthWrite = depthWrite,
			.depthOp = PipelineState::DepthOp::Less,
			.cullMode = cullMode,
			.outputFormatSize = 0,
			.outputFormatNorm = 0
		};
		state.setFormat<glm::u8vec4>();

		return state;
	}

	PipelineState solidState()
	{
		PipelineState state{
			.depthTest = true,
			.depthWrite = true,
			.depthOp = PipelineState::DepthOp::Less,
			.cullMode = PipelineState::CullMode::None,
			.outputFormatSize = 0,
			.outputFormatNorm = 0
		};
		state.setFormat<glm::u8vec4>();

		return state;
	}

	glm::mat4 paneTransform(const uint32_t index, const float edge)
	{
		glm::mat4 transform = glm::translate(glm::vec3((static_cast<float>(index) - 1.0f) * edge, 0.0f, -6.0f - static_cast<float>(index) * 4.0f));
		transform = glm::rotate(transform, glm::radians(28.0f * static_cast<float>(index + 1u)), glm::vec3(0.0f, 1.0f, 0.0f));

		return transform;
	}
}

ColorPipeline::VOutput ColorPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.normal = uni->normalMatrix * glm::vec4(vIn->normal, 0.0f);
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

BasicColorOut ColorPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
	const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);
	const glm::vec4 tex = uni->tex->sample(vOut->uvCoords, tpw, 2.f);

	return BasicColorOut{ glm::vec4(diffuse * glm::vec3{tex} * glm::vec3{uni->color}, uni->color.a) };
}

glm::vec2 ColorPipeline::getUV(const VOutput* vOut)
{
	return vOut->uvCoords;
}

InstancedColorPipeline::VOutput InstancedColorPipeline::vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = instance->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.normal = instance->normalMatrix * glm::vec4(vIn->normal, 0.0f);
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

BasicColorOut InstancedColorPipeline::fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw)
{
	const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
	const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);
	const glm::vec4 tex = uni->tex->sample(vOut->uvCoords, tpw, 2.f);

	return BasicColorOut{ glm::vec4(diffuse * glm::vec3{tex} * glm::vec3{instance->color}, instance->color.a) };
}

glm::vec2 InstancedColorPipeline::getUV(const VOutput* vOut)
{
	return vOut->uvCoords;
}

TranslucentPipeline::VOutput TranslucentPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.normal = uni->normalMatrix * glm::vec4(vIn->normal, 0.0f);
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

BasicColorOut TranslucentPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec4 tex = uni->tex->sample(vOut->uvCoords, tpw, 2.f);

	return BasicColorOut{ glm::vec4(glm::vec3{tex} * glm::vec3{uni->color}, uni->color.a) };
}

glm::vec2 TranslucentPipeline::getUV(const VOutput* vOut)
{
	return vOut->uvCoords;
}

glm::vec4 TranslucentPipeline::blendShader(const glm::vec4& src, const glm::vec4& dst, const Uniform* uni)
{
	const float srcAlpha = src.a * uni->color.a;
	const float invSrcAlpha = 1.0f - srcAlpha;

	return { glm::vec3(src) * srcAlpha + glm::vec3(dst) * invSrcAlpha, srcAlpha + dst.a * invSrcAlpha };
}

TerrainPipeline::VOutput TerrainPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.normal = uni->normalMatrix * glm::vec4(vIn->normal, 0.0f);
	vOut.color = vIn->color;

	return vOut;
}

BasicColorOut TerrainPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec3 normalizedNormal = glm::normalize(vOut->normal);
	const float diffuse = glm::max(glm::dot(normalizedNormal, -uni->lightDirection), uni->ambientMult);

	return BasicColorOut{ glm::vec4(glm::vec3{diffuse} * vOut->color, 1.0f) };
}

SolidPipeline::VOutput SolidPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);

	return vOut;
}

BasicColorOut SolidPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	return BasicColorOut{ uni->color };
}

PaletteDisplayPipeline::VOutput PaletteDisplayPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = glm::vec4(vIn->position, 1.0f);
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

BasicColorOut PaletteDisplayPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const uint32_t index = glm::min(static_cast<uint32_t>(vOut->uvCoords.x * static_cast<float>(uni->count)), uni->count - 1);

	return BasicColorOut{ glm::vec4(uni->palette[index], 1.0f) };
}

void PaletteComputePipeline::computeShader(const ComputeContext& ctx, const Uniform* uni)
{
	const uint32_t index = ctx.globalInvocationID.x;

	if (index >= uni->count)
		return;

	const float t = static_cast<float>(index) / static_cast<float>(uni->count);
	uni->palette[index] = glm::vec3(0.5f + 0.5f * std::sin(t * 12.0f), t * t, 0.5f + 0.5f * std::cos(t * 9.0f));
}

CameraSetup cubeCameraSetup()
{
	return { .position = { 0.0f, 0.0f, 5.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
}

CameraSetup terrainCameraSetup()
{
	return { .position = { 0.0f, 46.0f, 118.0f }, .direction = glm::normalize(glm::vec3(0.0f, -46.0f, -118.0f)), .fov = 60.0f };
}

CameraSetup torusKnotCameraSetup()
{
	return { .position = { 0.0f, 9.0f, 24.0f }, .direction = glm::normalize(glm::vec3(0.0f, -9.0f, -24.0f)), .fov = 60.0f };
}

CameraSetup blendCameraSetup()
{
	return { .position = { 0.0f, 0.0f, 9.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
}

CameraSetup mipDebugCameraSetup()
{
	return { .position = { 0.0f, 1.2f, 3.5f }, .direction = glm::normalize(glm::vec3(0.0f, -0.25f, -1.0f)), .fov = 60.0f };
}

CameraSetup stressCameraSetup()
{
	return { .position = { 0.0f, 0.0f, 14.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
}

CameraSetup computeCameraSetup()
{
	return { .position = { 0.0f, 0.0f, 3.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
}

void makeCubeMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices)
{
	vertices = {
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .uvcoords = {0.0f, 1.0f} },

		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .uvcoords = {0.0f, 1.0f} },

		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		{.position = {-1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f,  1.0f,  1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f,  1.0f, -1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} },

		{.position = {-1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {0.0f, 0.0f} },
		{.position = {-1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {1.0f, 0.0f} },
		{.position = { 1.0f, -1.0f, -1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {1.0f, 1.0f} },
		{.position = { 1.0f, -1.0f,  1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uvcoords = {0.0f, 1.0f} }
	};

	indices = {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4,
		8, 9, 10, 10, 11, 8,
		12, 13, 14, 14, 15, 12,
		16, 17, 18, 18, 19, 16,
		20, 21, 22, 22, 23, 20
	};
}

void makeTorusKnotMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, const uint32_t segments, const uint32_t sides, const float radius, const float scale)
{
	constexpr float twoPi = 6.28318530718f;

	vertices.clear();
	indices.clear();
	vertices.reserve(static_cast<size_t>(segments) * sides);
	indices.reserve(static_cast<size_t>(segments) * sides * 6);

	const auto curvePoint = [](const float u)
	{
		const float tubeCentre = 2.0f + std::cos(3.0f * u);
		return glm::vec3(tubeCentre * std::cos(2.0f * u), std::sin(3.0f * u), tubeCentre * std::sin(2.0f * u));
	};

	for (uint32_t i = 0; i < segments; ++i)
	{
		const float u = static_cast<float>(i) / static_cast<float>(segments) * twoPi;
		const glm::vec3 centre = curvePoint(u);
		const glm::vec3 tangent = glm::normalize(curvePoint(u + 0.01f) - curvePoint(u - 0.01f));
		const glm::vec3 normal = glm::normalize(glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::vec3 binormal = glm::cross(tangent, normal);

		for (uint32_t j = 0; j < sides; ++j)
		{
			const float v = static_cast<float>(j) / static_cast<float>(sides) * twoPi;
			const glm::vec3 offset = std::cos(v) * normal + std::sin(v) * binormal;

			ColorPipeline::VInput vertex{};
			vertex.position = (centre + offset * radius) * scale;
			vertex.normal = offset;
			vertex.uvcoords = { static_cast<float>(i) / static_cast<float>(segments) * 6.0f, static_cast<float>(j) / static_cast<float>(sides) };

			vertices.push_back(vertex);
		}
	}

	for (uint32_t i = 0; i < segments; ++i)
	{
		for (uint32_t j = 0; j < sides; ++j)
		{
			const uint32_t nextI = (i + 1) % segments;
			const uint32_t nextJ = (j + 1) % sides;

			const uint32_t a = i * sides + j;
			const uint32_t b = nextI * sides + j;
			const uint32_t c = nextI * sides + nextJ;
			const uint32_t d = i * sides + nextJ;

			indices.push_back(a);
			indices.push_back(d);
			indices.push_back(c);

			indices.push_back(c);
			indices.push_back(b);
			indices.push_back(a);
		}
	}
}

void makeTerrainMesh(std::vector<TerrainPipeline::VInput>& vertices, std::vector<uint32_t>& indices, const uint32_t cells, const float extent)
{
	const auto height = [](const float x, const float z)
	{
		return 9.0f * std::sin(x * 0.045f) * std::cos(z * 0.038f)
			+ 4.0f * std::sin(x * 0.11f + z * 0.07f)
			+ 2.0f * std::cos(x * 0.19f - z * 0.23f);
	};

	const auto normal = [&height](const float x, const float z, const float step)
	{
		const float left = height(x - step, z);
		const float right = height(x + step, z);
		const float down = height(x, z - step);
		const float up = height(x, z + step);

		return glm::normalize(glm::vec3(left - right, 2.0f * step, down - up));
	};

	const uint32_t side = cells + 1;
	const float cellSize = extent / static_cast<float>(cells);
	const float half = extent * 0.5f;

	vertices.clear();
	indices.clear();
	vertices.reserve(static_cast<size_t>(side) * side);
	indices.reserve(static_cast<size_t>(cells) * cells * 6);

	for (uint32_t z = 0; z < side; ++z)
	{
		for (uint32_t x = 0; x < side; ++x)
		{
			const float worldX = -half + static_cast<float>(x) * cellSize;
			const float worldZ = -half + static_cast<float>(z) * cellSize;
			const float worldY = height(worldX, worldZ);

			const float blend = glm::clamp((worldY + 5.0f) / 17.0f, 0.0f, 1.0f);

			TerrainPipeline::VInput vertex{};
			vertex.position = { worldX, worldY, worldZ };
			vertex.normal = normal(worldX, worldZ, cellSize * 0.5f);
			vertex.color = glm::mix(glm::vec3(0.20f, 0.34f, 0.18f), glm::vec3(0.78f, 0.74f, 0.66f), blend);

			vertices.push_back(vertex);
		}
	}

	for (uint32_t z = 0; z < cells; ++z)
	{
		for (uint32_t x = 0; x < cells; ++x)
		{
			const uint32_t a = z * side + x;
			const uint32_t b = z * side + x + 1;
			const uint32_t c = (z + 1) * side + x + 1;
			const uint32_t d = (z + 1) * side + x;

			indices.push_back(a);
			indices.push_back(c);
			indices.push_back(b);

			indices.push_back(c);
			indices.push_back(a);
			indices.push_back(d);
		}
	}
}

void makeBlendMesh(std::vector<TranslucentPipeline::VInput>& vertices, const uint32_t paneCount)
{
	constexpr float paneSize = 3.4f;
	constexpr float paneEdge = 3.6f;

	vertices.clear();
	vertices.reserve(static_cast<size_t>(paneCount) * 6);

	for (uint32_t i = 0; i < paneCount; ++i)
	{
		const glm::mat4 transform = paneTransform(i, paneEdge);

		const glm::vec3 corners[4] = {
			glm::vec3(transform * glm::vec4(-paneSize, -paneSize, 0.0f, 1.0f)),
			glm::vec3(transform * glm::vec4( paneSize, -paneSize, 0.0f, 1.0f)),
			glm::vec3(transform * glm::vec4( paneSize,  paneSize, 0.0f, 1.0f)),
			glm::vec3(transform * glm::vec4(-paneSize,  paneSize, 0.0f, 1.0f))
		};

		const glm::vec2 uvs[4] = {
			{ 0.0f, 0.0f }, { 2.0f, 0.0f }, { 2.0f, 2.0f }, { 0.0f, 2.0f }
		};

		for (const uint32_t index : { 0u, 1u, 2u, 2u, 3u, 0u })
		{
			TranslucentPipeline::VInput vertex{};
			vertex.position = corners[index];
			vertex.normal = glm::normalize(glm::vec3(transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
			vertex.uvcoords = uvs[index];

			vertices.push_back(vertex);
		}
	}
}

void makeMipDebugMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, const uint32_t slabCount)
{
	vertices.clear();
	indices.clear();
	vertices.reserve(static_cast<size_t>(slabCount) * 4);
	indices.reserve(static_cast<size_t>(slabCount) * 6);

	for (uint32_t i = 0; i < slabCount; ++i)
	{
		const float near = 1.0f + static_cast<float>(i) * 2.0f;
		const float far = near + 2.0f;
		const uint32_t base = static_cast<uint32_t>(vertices.size());

		const glm::vec3 corners[4] = {
			{ -1.0f, 0.0f, -near },
			{  1.0f, 0.0f, -near },
			{  1.0f, 0.0f, -far },
			{ -1.0f, 0.0f, -far }
		};

		for (uint32_t j = 0; j < 4; ++j)
		{
			ColorPipeline::VInput vertex{};
			vertex.position = corners[j];
			vertex.normal = { 0.0f, 1.0f, 0.0f };
			vertex.uvcoords = { (corners[j].x + 1.0f) * 0.5f, -(corners[j].z + near) * 0.5f };

			vertices.push_back(vertex);
		}

		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 1);

		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 3);
	}
}

void makeStressMesh(std::vector<SolidPipeline::VInput>& vertices, std::vector<SolidPipeline::Uniform>& uniforms, const uint32_t drawCount, const uint32_t padFillerCount)
{
	std::mt19937 random{ 12345u };
	std::uniform_real_distribution<float> position(-14.0f, 14.0f);
	std::uniform_real_distribution<float> size(0.15f, 1.1f);
	std::uniform_real_distribution<float> depth(-30.0f, -3.0f);
	std::uniform_real_distribution<float> channel(0.0f, 1.0f);

	vertices.clear();
	uniforms.clear();
	vertices.reserve(static_cast<size_t>(drawCount) * 6 + static_cast<size_t>(padFillerCount) * 3);
	uniforms.reserve(drawCount);

	for (uint32_t i = 0; i < drawCount; ++i)
	{
		const float centreX = position(random);
		const float centreY = position(random);
		const float centreZ = depth(random);
		const float halfSize = size(random);

		const glm::vec3 corners[4] = {
			{ centreX - halfSize, centreY - halfSize, centreZ },
			{ centreX + halfSize, centreY - halfSize, centreZ },
			{ centreX + halfSize, centreY + halfSize, centreZ },
			{ centreX - halfSize, centreY + halfSize, centreZ }
		};

		for (const uint32_t index : { 0u, 1u, 2u, 2u, 3u, 0u })
			vertices.push_back({ .position = corners[index] });

		uniforms.push_back({ .modelViewProjectionMatrix = {}, .color = { channel(random), channel(random), channel(random), 1.0f } });
	}

	for (uint32_t i = 0; i < padFillerCount * 3; ++i)
	{
		const float offset = (i % 3 == 0) ? -5000.0f : ((i % 3 == 1) ? 0.0f : 5000.0f);
		vertices.push_back({ .position = { offset, 0.0f, -3.0f } });
	}
}

void makeComputePaletteMesh(std::vector<PaletteDisplayPipeline::VInput>& vertices)
{
	vertices = {
		{ .position = { -1.0f, -1.0f, 0.5f }, .uvcoords = { 0.0f, 0.0f } },
		{ .position = {  1.0f, -1.0f, 0.5f }, .uvcoords = { 1.0f, 0.0f } },
		{ .position = { -1.0f,  1.0f, 0.5f }, .uvcoords = { 0.0f, 1.0f } },
		{ .position = {  1.0f, -1.0f, 0.5f }, .uvcoords = { 1.0f, 0.0f } },
		{ .position = {  1.0f,  1.0f, 0.5f }, .uvcoords = { 1.0f, 1.0f } },
		{ .position = { -1.0f,  1.0f, 0.5f }, .uvcoords = { 0.0f, 1.0f } }
	};
}

CubesScene::CubesScene(CommandBuffer& commandBuffer)
	: texture("../texture.png"),
	  recording(commandBuffer.registerPipeline<InstancedColorPipeline>(colorState(PipelineState::CullMode::Back, true)))
{
	makeCubeMesh(vertices, indices);

	texture.setSampler(TRILINEAR, REPEAT);

	uniform.tex = &texture;
	uniform.lightDirection = kLightDirection;
	uniform.ambientMult = 0.1f;

	recording.reserve(1, 1);
}

const char* CubesScene::name()
{
	return "cubes";
}

CameraSetup CubesScene::cameraSetup()
{
	return cubeCameraSetup();
}

void CubesScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	recording.clear();

	uint32_t instanceIndex = 0;
	for (int z = 0; z <= 4; z++)
	{
		for (int x = -2; x <= 2; x++)
		{
			for (int y = -2; y <= 2; y++)
			{
				glm::mat4 modelMat = glm::translate(glm::vec3(static_cast<float>(x) * 3.f, static_cast<float>(y) * 3.f, -4.f - static_cast<float>(z) * 3.f));
				modelMat = glm::rotate(modelMat, glm::radians(time * 5.f), glm::vec3(0.0f, 1.0f, 0.0f));
				modelMat = glm::rotate(modelMat, glm::radians(time * 3.f), glm::vec3(1.0f, 0.0f, 0.0f));

				instances[instanceIndex++] = {
					.modelViewProjectionMatrix = camera.getVPMatrix() * modelMat,
					.normalMatrix = glm::transpose(glm::inverse(modelMat)),
					.color = glm::vec4(static_cast<float>(x + 2) / 5.0f, static_cast<float>(y + 2) / 5.0f, 1 - (static_cast<float>(z) / 5.0f), 1.0f),
				};
			}
		}
	}

	recording.bindUniform(uniform);
	recording.drawIndexedInstanced(vertices, indices, instances);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

TerrainScene::TerrainScene(CommandBuffer& commandBuffer)
	: recording(commandBuffer.registerPipeline<TerrainPipeline>(colorState(PipelineState::CullMode::None, true)))
{
	makeTerrainMesh(vertices, indices, 192, 300.0f);

	uniform.lightDirection = kLightDirection;
	uniform.ambientMult = 0.22f;

	recording.reserve(1, 1);
}

const char* TerrainScene::name()
{
	return "terrain";
}

CameraSetup TerrainScene::cameraSetup()
{
	return terrainCameraSetup();
}

void TerrainScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.42f, 0.58f, 0.82f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	recording.clear();

	uniform.modelViewProjectionMatrix = camera.getVPMatrix();
	uniform.normalMatrix = glm::mat4(1.0f);

	recording.bindUniform(uniform);
	recording.drawIndexed(vertices, indices);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

TorusKnotScene::TorusKnotScene(CommandBuffer& commandBuffer)
	: texture("../texture.png"), recording(commandBuffer.registerPipeline<ColorPipeline>(colorState(PipelineState::CullMode::None, true)))
{
	makeTorusKnotMesh(vertices, indices, 192, 24, 0.42f, 3.4f);

	texture.setSampler(TRILINEAR, REPEAT);

	uniform.tex = &texture;
	uniform.lightDirection = kLightDirection;
	uniform.ambientMult = 0.15f;
	uniform.color = glm::vec4(1.0f);

	recording.reserve(1, 1);
}

const char* TorusKnotScene::name()
{
	return "torusKnot";
}

CameraSetup TorusKnotScene::cameraSetup()
{
	return torusKnotCameraSetup();
}

void TorusKnotScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.06f, 0.07f, 0.12f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	recording.clear();

	glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), glm::radians(time * 7.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	modelMat = glm::rotate(modelMat, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	uniform.modelViewProjectionMatrix = camera.getVPMatrix() * modelMat;
	uniform.normalMatrix = glm::transpose(glm::inverse(modelMat));

	recording.bindUniform(uniform);
	recording.drawIndexed(vertices, indices);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

BlendScene::BlendScene(CommandBuffer& commandBuffer)
	: texture("../texture.png"),
	  opaqueRecording(commandBuffer.registerPipeline<InstancedColorPipeline>(colorState(PipelineState::CullMode::Back, true))),
	  paneRecording(commandBuffer.registerPipeline<TranslucentPipeline>(colorState(PipelineState::CullMode::None, false)))
{
	makeCubeMesh(cubeVertices, cubeIndices);
	makeBlendMesh(paneVertices, 3);

	texture.setSampler(TRILINEAR, REPEAT);

	opaqueUniform.tex = &texture;
	opaqueUniform.lightDirection = kLightDirection;
	opaqueUniform.ambientMult = 0.12f;

	paneUniform.tex = &texture;
	paneUniform.lightDirection = kLightDirection;
	paneUniform.color = glm::vec4(0.62f, 0.78f, 0.92f, 0.45f);

	opaqueRecording.reserve(1, 1);
	paneRecording.reserve(1, 1);
}

const char* BlendScene::name()
{
	return "blend";
}

CameraSetup BlendScene::cameraSetup()
{
	return blendCameraSetup();
}

void BlendScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.03f, 0.03f, 0.05f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	opaqueRecording.clear();
	paneRecording.clear();

	uint32_t instanceIndex = 0;
	for (int z = 0; z <= 2; z++)
	{
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				glm::mat4 modelMat = glm::translate(glm::vec3(static_cast<float>(x) * 4.2f, static_cast<float>(y) * 4.2f, -6.0f - static_cast<float>(z) * 4.5f));
				modelMat = glm::rotate(modelMat, glm::radians(time * 4.0f + static_cast<float>(x * 3 + y) * 25.0f), glm::vec3(0.0f, 1.0f, 1.0f));

				opaqueInstances[instanceIndex++] = {
					.modelViewProjectionMatrix = camera.getVPMatrix() * modelMat,
					.normalMatrix = glm::transpose(glm::inverse(modelMat)),
					.color = glm::vec4(0.35f + static_cast<float>(x + 1) * 0.3f, 0.4f + static_cast<float>(y + 1) * 0.25f, 0.55f + static_cast<float>(z) * 0.2f, 1.0f),
				};
			}
		}
	}

	opaqueRecording.bindUniform(opaqueUniform);
	opaqueRecording.drawIndexedInstanced(cubeVertices, cubeIndices, opaqueInstances);

	paneUniform.modelViewProjectionMatrix = camera.getVPMatrix();
	paneUniform.normalMatrix = glm::mat4(1.0f);

	paneRecording.bindUniform(paneUniform);
	paneRecording.draw(paneVertices);

	opaqueRecording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
	paneRecording.commit(commandBuffer, framebuffer, &depthBuffer);
}

MipDebugScene::MipDebugScene(CommandBuffer& commandBuffer)
	: texture("../texture.png"),
	  recording(commandBuffer.registerPipeline<ColorPipeline>(colorState(PipelineState::CullMode::None, true)))
{
	makeMipDebugMesh(vertices, indices, 14);

	texture.setSampler(TRILINEAR, REPEAT);

	uniform.tex = &texture;
	uniform.lightDirection = glm::vec3(0.0f, 0.0f, 0.0f);
	uniform.ambientMult = 1.0f;
	uniform.color = glm::vec4(1.0f);

	recording.reserve(1, 1);
}

const char* MipDebugScene::name()
{
	return "mipDebug";
}

CameraSetup MipDebugScene::cameraSetup()
{
	return mipDebugCameraSetup();
}

void MipDebugScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.05f, 0.05f, 0.06f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	const bool wantDebug = std::fmod(time, 8.0f) > 4.0f;

	if (wantDebug != debugMode)
	{
		texture.toggleDebugMode();
		debugMode = wantDebug;
	}

	recording.clear();

	uniform.modelViewProjectionMatrix = camera.getVPMatrix();
	uniform.normalMatrix = glm::mat4(1.0f);

	recording.bindUniform(uniform);
	recording.drawIndexed(vertices, indices);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

StressScene::StressScene(CommandBuffer& commandBuffer)
	: recording(commandBuffer.registerPipeline<SolidPipeline>(solidState()))
{
	makeStressMesh(vertices, uniforms, kDrawCount, kPadFillerCount);
}

const char* StressScene::name()
{
	return "stress";
}

CameraSetup StressScene::cameraSetup()
{
	return stressCameraSetup();
}

void StressScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	recording.clear();

	const glm::mat4 viewProjection = camera.getVPMatrix();

	for (uint32_t i = 0; i < uniforms.size(); ++i)
	{
		uniforms[i].modelViewProjectionMatrix = viewProjection;

		recording.bindUniform(uniforms[i]);
		recording.draw({ vertices.data() + static_cast<size_t>(i) * 6, 6 });
	}

	const uint32_t fillerStart = static_cast<uint32_t>(uniforms.size()) * 6;

	if constexpr (kPadFillerCount > 0)
	{
		SolidPipeline::Uniform fillerUniform{ .modelViewProjectionMatrix = viewProjection, .color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) };

		recording.bindUniform(fillerUniform);
		recording.draw({ vertices.data() + fillerStart, static_cast<size_t>(kPadFillerCount) * 3 });
	}

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

ComputeScene::ComputeScene(CommandBuffer& commandBuffer)
	: recording(commandBuffer.registerPipeline<PaletteDisplayPipeline>(solidState()))
{
	palette.resize(kPaletteSize);
	makeComputePaletteMesh(vertices);

	computeUniform.palette = palette.data();
	computeUniform.count = kPaletteSize;

	displayUniform.palette = palette.data();
	displayUniform.count = kPaletteSize;

	recording.reserve(1, 1);
}

const char* ComputeScene::name()
{
	return "compute";
}

CameraSetup ComputeScene::cameraSetup()
{
	return computeCameraSetup();
}

void ComputeScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	std::ranges::fill(palette, glm::vec3(0.0f));

	commandBuffer.commitCompute<PaletteComputePipeline>(computeUniform, { kLocalGroupSize, 1, 1 }, { static_cast<float>(kPaletteSize / kLocalGroupSize), 1.0f, 1.0f });

	recording.clear();

	recording.bindUniform(displayUniform);
	recording.draw(vertices);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

namespace
{
	constexpr float kPi = 3.14159265358979f;
	constexpr glm::vec3 kPbrLightColor{ 3.0f, 2.95f, 2.85f };
	constexpr glm::vec3 kPbrSkyColor{ 0.35f, 0.42f, 0.55f };
	constexpr glm::vec3 kPbrGroundColor{ 0.12f, 0.10f, 0.09f };
}

PbrPipeline::VOutput PbrPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.worldPosition = glm::vec3(uni->modelMatrix * glm::vec4(vIn->position, 1.0f));
	vOut.worldNormal = glm::mat3(uni->normalMatrix) * vIn->normal;
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

std::optional<BasicColorOut> PbrPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec3 normal = glm::normalize(vOut->worldNormal);
	const glm::vec3 view = glm::normalize(uni->cameraPosition - vOut->worldPosition);
	const glm::vec3 light = glm::normalize(-uni->lightDirection);

	glm::vec4 baseColor = uni->baseColorFactor;

	if (uni->baseColorTexture)
		baseColor *= uni->baseColorTexture->sample(vOut->uvCoords);

	if (uni->alphaMask && baseColor.a < uni->alphaCutoff)
		return std::nullopt;

	float metallic = uni->metallicFactor;
	float roughness = uni->roughnessFactor;

	if (uni->metallicRoughnessTexture)
	{
		const glm::vec4 packed = uni->metallicRoughnessTexture->sample(vOut->uvCoords);
		roughness *= packed.g;
		metallic *= packed.b;
	}

	metallic = glm::clamp(metallic, 0.0f, 1.0f);
	roughness = glm::clamp(roughness, 0.045f, 1.0f);

	const glm::vec3 albedo = glm::vec3(baseColor);
	const glm::vec3 fresnelBase = glm::mix(glm::vec3(0.04f), albedo, metallic);
	const float alpha = roughness * roughness;
	const float alphaSquared = alpha * alpha;
	const glm::vec3 halfway = glm::normalize(view + light);

	const float noL = glm::max(glm::dot(normal, light), 0.0f);
	const float noV = glm::max(glm::dot(normal, view), 0.0f);
	const float noH = glm::max(glm::dot(normal, halfway), 0.0f);
	const float voH = glm::max(glm::dot(view, halfway), 0.0f);

	const float distributionDenominator = noH * noH * (alphaSquared - 1.0f) + 1.0f;
	const float distribution = alphaSquared / glm::max(kPi * distributionDenominator * distributionDenominator, 1.0e-6f);
	const float geometryK = alpha * 0.5f;
	const float geometry = (noV / glm::max(noV * (1.0f - geometryK) + geometryK, 1.0e-6f))
		* (noL / glm::max(noL * (1.0f - geometryK) + geometryK, 1.0e-6f));
	const glm::vec3 fresnel = fresnelBase + (glm::vec3(1.0f) - fresnelBase) * glm::pow(1.0f - voH, 5.0f);

	const glm::vec3 specular = (distribution * geometry) * fresnel / glm::max(4.0f * noV * noL, 1.0e-6f);
	const glm::vec3 diffuse = (glm::vec3(1.0f) - fresnel) * (1.0f - metallic) * albedo / kPi;

	glm::vec3 color = (diffuse + specular) * kPbrLightColor * noL;

	float occlusion = 1.0f;

	if (uni->occlusionTexture)
		occlusion = glm::mix(1.0f, uni->occlusionTexture->sample(vOut->uvCoords).r, uni->occlusionStrength);

	const glm::vec3 ambient = glm::mix(kPbrGroundColor, kPbrSkyColor, normal.y * 0.5f + 0.5f) * occlusion;

	color += ambient * (albedo * (1.0f - metallic) + fresnelBase * (1.0f - roughness * 0.7f));

	if (uni->emissiveTexture)
		color += uni->emissiveFactor * glm::vec3(uni->emissiveTexture->sample(vOut->uvCoords));
	else
		color += uni->emissiveFactor;

	color = color / (color + glm::vec3(1.0f));

	return BasicColorOut{ glm::vec4{ glm::clamp(color, 0.0f, 1.0f), baseColor.a } };
}

GltfScene::GltfScene(CommandBuffer& commandBuffer)
	: model(loadGltf(kModelPath)),
	  recording(commandBuffer.registerPipeline<PbrPipeline>(colorState(PipelineState::CullMode::Back, true))),
	  doubleSidedRecording(commandBuffer.registerPipeline<PbrPipeline>(colorState(PipelineState::CullMode::None, true)))
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(model.primitives.size()); ++i)
	{
		const int32_t material = model.primitives[i].material;
		const bool doubleSided = material >= 0 && static_cast<size_t>(material) < model.materials.size()
			&& model.materials[material].doubleSided;

		if (doubleSided)
			doubleSidedPrimitives.push_back(i);
		else
			singleSidedPrimitives.push_back(i);
	}

	recording.reserve(singleSidedPrimitives.size(), singleSidedPrimitives.size());
	doubleSidedRecording.reserve(doubleSidedPrimitives.size(), doubleSidedPrimitives.size());

	uniform.lightDirection = kLightDirection;
}

const char* GltfScene::name()
{
	return "gltf";
}

CameraSetup GltfScene::cameraSetup()
{
	const glm::vec3 position{ 1.8f, 1.15f, 2.7f };

	return { .position = position, .direction = glm::normalize(-position), .fov = 45.0f };
}

void GltfScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{
		.colors = { glm::vec4(0.02f, 0.02f, 0.025f, 1.0f) },
		.depth = std::numeric_limits<float>::infinity()
	};

	const auto resolveTexture = [this](const int32_t textureIndex) -> Texture<glm::u8vec4>*
	{
		if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= model.textures.size())
			return nullptr;

		return model.textures[static_cast<size_t>(textureIndex)].get();
	};

	const glm::mat4 modelMatrix = glm::rotate(glm::mat4{ 1.0f }, glm::radians(time * kRotateSpeed), glm::vec3(0.0f, 1.0f, 0.0f));

	uniform.modelViewProjectionMatrix = camera.getVPMatrix() * modelMatrix;
	uniform.modelMatrix = modelMatrix;
	uniform.normalMatrix = glm::transpose(glm::inverse(modelMatrix));
	uniform.cameraPosition = camera.getPosition();

	recording.clear();
	doubleSidedRecording.clear();

	const auto drawPrimitive = [&](const uint32_t index, CommandBufferRecording<PbrPipeline>& target)
	{
		const GltfPrimitive& primitive = model.primitives[index];
		const GltfMaterial material = primitive.material >= 0
			? model.materials[static_cast<size_t>(primitive.material)]
			: GltfMaterial{};

		uniform.baseColorFactor = material.baseColorFactor;
		uniform.emissiveFactor = material.emissiveFactor;
		uniform.metallicFactor = material.metallicFactor;
		uniform.roughnessFactor = material.roughnessFactor;
		uniform.occlusionStrength = material.occlusionStrength;
		uniform.alphaMask = material.alphaMask;
		uniform.alphaCutoff = material.alphaCutoff;
		uniform.baseColorTexture = resolveTexture(material.baseColorTexture);
		uniform.metallicRoughnessTexture = resolveTexture(material.metallicRoughnessTexture);
		uniform.emissiveTexture = resolveTexture(material.emissiveTexture);
		uniform.occlusionTexture = resolveTexture(material.occlusionTexture);

		target.bindUniform(uniform);
		target.drawIndexed(primitive.vertices, primitive.indices);
	};

	for (const uint32_t index : singleSidedPrimitives)
		drawPrimitive(index, recording);

	for (const uint32_t index : doubleSidedPrimitives)
		drawPrimitive(index, doubleSidedRecording);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);

	if (!doubleSidedPrimitives.empty())
		doubleSidedRecording.commit(commandBuffer, framebuffer, &depthBuffer, ClearState{});
}
