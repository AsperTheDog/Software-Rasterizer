#include "multipass.hpp"

#include <gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

const std::array<FullscreenVertex, 3> kFullscreenTriangle = { {
	{ { -1.0f, -1.0f } },
	{ {  3.0f, -1.0f } },
	{ { -1.0f,  3.0f } }
} };

namespace
{
	constexpr float kInfinity = std::numeric_limits<float>::infinity();
	constexpr float kPi = 3.14159265358979f;

	PipelineState makeState(const PipelineState::CullMode cullMode, const bool depthTest, const bool depthWrite, const bool hasColor)
	{
		PipelineState state{
			.depthTest = depthTest,
			.depthWrite = depthWrite,
			.depthOp = PipelineState::DepthOp::Less,
			.cullMode = cullMode,
			.outputFormatSize = 0,
			.outputFormatNorm = 0
		};
		if (hasColor)
			state.setFormat<glm::u8vec4>();

		return state;
	}

	FullscreenVOut fullscreenVertex(const FullscreenVertex* vIn)
	{
		FullscreenVOut vOut{};
		vOut.position = glm::vec4(vIn->position, 0.0f, 1.0f);
		vOut.uv = glm::vec2(vIn->position.x * 0.5f + 0.5f, 0.5f - vIn->position.y * 0.5f);

		return vOut;
	}

	float shadowVisibility(const Texture<glm::vec1>& shadowMap, const glm::vec4& lightPosition, const float bias)
	{
		const glm::vec3 ndc = glm::vec3(lightPosition) / lightPosition.w;
		const glm::vec2 uv = glm::vec2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

		if (uv.x < 0.0f || uv.y < 0.0f || uv.x >= 1.0f || uv.y >= 1.0f)
			return 1.0f;

		const glm::ivec2 size = glm::ivec2(shadowMap.getSize());
		const glm::ivec2 centre = glm::ivec2(uv * glm::vec2(size));

		float lit = 0.0f;
		for (int32_t dy = -1; dy <= 1; ++dy)
		{
			for (int32_t dx = -1; dx <= 1; ++dx)
			{
				const glm::ivec2 texel = glm::clamp(centre + glm::ivec2(dx, dy), glm::ivec2(0), size - 1);
				lit += ndc.z - bias <= shadowMap.at(glm::uvec2(texel)).x ? 1.0f : 0.0f;
			}
		}

		return lit / 9.0f;
	}

	void makeGroundMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, const float halfExtent)
	{
		const glm::vec3 up(0.0f, 1.0f, 0.0f);

		vertices = {
			{ .position = { -halfExtent, 0.0f, -halfExtent }, .normal = up, .uvcoords = { 0.0f, 0.0f } },
			{ .position = { -halfExtent, 0.0f,  halfExtent }, .normal = up, .uvcoords = { 0.0f, 1.0f } },
			{ .position = {  halfExtent, 0.0f,  halfExtent }, .normal = up, .uvcoords = { 1.0f, 1.0f } },
			{ .position = {  halfExtent, 0.0f, -halfExtent }, .normal = up, .uvcoords = { 1.0f, 0.0f } }
		};
		indices = { 0, 1, 2, 2, 3, 0 };
	}

	void makeScreenMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, const float halfExtent)
	{
		const glm::vec3 forward(0.0f, 0.0f, 1.0f);

		vertices = {
			{ .position = { -halfExtent, -halfExtent, 0.0f }, .normal = forward, .uvcoords = { 0.0f, 1.0f } },
			{ .position = {  halfExtent, -halfExtent, 0.0f }, .normal = forward, .uvcoords = { 1.0f, 1.0f } },
			{ .position = {  halfExtent,  halfExtent, 0.0f }, .normal = forward, .uvcoords = { 1.0f, 0.0f } },
			{ .position = { -halfExtent,  halfExtent, 0.0f }, .normal = forward, .uvcoords = { 0.0f, 0.0f } }
		};
		indices = { 0, 1, 2, 2, 3, 0 };
	}
}

PipelineState fullscreenState()
{
	return makeState(PipelineState::CullMode::None, false, false, true);
}

Texture<glm::u8vec4>& ensureTarget(std::optional<Texture<glm::u8vec4>>& target, const glm::uvec2 size)
{
	if (!target || target->getSize() != size)
	{
		target.emplace(size, glm::u8vec4(0, 0, 0, 255));
		target->setFormat(SRGB);
		target->setSampler(BILINEAR, CLAMP);
	}

	return *target;
}

PostFxPipeline::VOutput PostFxPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	return fullscreenVertex(vIn);
}

BasicColorOut PostFxPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec2 centred = vOut->uv - 0.5f;
	const glm::vec2 shift = centred * uni->aberration;

	const glm::vec3 color(
		uni->source->sample(vOut->uv + shift).r,
		uni->source->sample(vOut->uv).g,
		uni->source->sample(vOut->uv - shift).b);

	const float vignette = glm::clamp(1.0f - glm::dot(centred, centred) * uni->vignette, 0.0f, 1.0f);

	return BasicColorOut{ glm::vec4(color * vignette, 1.0f) };
}

BrightPassPipeline::VOutput BrightPassPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	return fullscreenVertex(vIn);
}

BasicColorOut BrightPassPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec2 offset = uni->texelSize;

	const glm::vec3 color = (glm::vec3(uni->source->sample(vOut->uv + glm::vec2(-offset.x, -offset.y)))
		+ glm::vec3(uni->source->sample(vOut->uv + glm::vec2(offset.x, -offset.y)))
		+ glm::vec3(uni->source->sample(vOut->uv + glm::vec2(-offset.x, offset.y)))
		+ glm::vec3(uni->source->sample(vOut->uv + glm::vec2(offset.x, offset.y)))) * 0.25f;

	const float peak = glm::max(color.r, glm::max(color.g, color.b));
	const float scale = glm::max(peak - uni->threshold, 0.0f) / glm::max(peak, 1e-4f);

	return BasicColorOut{ glm::vec4(color * scale, 1.0f) };
}

BlurPipeline::VOutput BlurPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	return fullscreenVertex(vIn);
}

BasicColorOut BlurPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	constexpr float offsets[2] = { 1.3846153846f, 3.2307692308f };
	constexpr float weights[2] = { 0.3162162162f, 0.0702702703f };

	glm::vec3 color = glm::vec3(uni->source->sample(vOut->uv)) * 0.2270270270f;
	for (uint32_t i = 0; i < 2; ++i)
	{
		const glm::vec2 shift = uni->step * offsets[i];
		color += (glm::vec3(uni->source->sample(vOut->uv + shift)) + glm::vec3(uni->source->sample(vOut->uv - shift))) * weights[i];
	}

	return BasicColorOut{ glm::vec4(color, 1.0f) };
}

CompositePipeline::VOutput CompositePipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	return fullscreenVertex(vIn);
}

BasicColorOut CompositePipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec3 scene = glm::vec3(uni->scene->sample(vOut->uv));
	const glm::vec3 bloom = glm::vec3(uni->bloom->sample(vOut->uv));

	return BasicColorOut{ glm::vec4(scene + bloom * uni->intensity, 1.0f) };
}

ShadowCasterPipeline::VOutput ShadowCasterPipeline::vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = instance->lightMatrix * glm::vec4(vIn->position, 1.0f);

	return vOut;
}

ShadowedPipeline::VOutput ShadowedPipeline::vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = instance->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.worldPosition = instance->modelMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.worldNormal = instance->modelMatrix * glm::vec4(vIn->normal, 0.0f);

	const float facing = glm::max(glm::dot(glm::normalize(vOut.worldNormal), -uni->lightDirection), 0.0f);
	const float sinTheta = std::sqrt(1.0f - facing * facing);
	vOut.lightPosition = instance->lightMatrix * glm::vec4(vIn->position + vIn->normal * (uni->normalOffset * sinTheta), 1.0f);

	return vOut;
}

BasicColorOut ShadowedPipeline::fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw)
{
	const glm::vec3 normal = glm::normalize(vOut->worldNormal);
	const float facing = glm::max(glm::dot(normal, -uni->lightDirection), 0.0f);

	const float visibility = shadowVisibility(*uni->shadowMap, vOut->lightPosition, 0.0015f);

	const float checker = std::fmod(std::floor(vOut->worldPosition.x) + std::floor(vOut->worldPosition.z), 2.0f) != 0.0f ? 1.0f : 0.0f;
	const float albedo = 1.0f - instance->checkerStrength * checker;

	const float light = uni->ambientMult + (1.0f - uni->ambientMult) * facing * visibility;

	return BasicColorOut{ glm::vec4(glm::vec3(instance->color) * albedo * light, instance->color.a) };
}

EmissivePipeline::VOutput EmissivePipeline::vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->viewProjectionMatrix * (instance->modelMatrix * glm::vec4(vIn->position, 1.0f));
	vOut.normal = instance->modelMatrix * glm::vec4(vIn->normal, 0.0f);

	return vOut;
}

BasicColorOut EmissivePipeline::fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw)
{
	const float facing = glm::max(glm::dot(glm::normalize(vOut->normal), -uni->lightDirection), 0.0f);

	return BasicColorOut{ glm::vec4(glm::vec3(instance->color) * (0.6f + 0.4f * facing), instance->color.a) };
}

ScreenPipeline::VOutput ScreenPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = uni->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.uvCoords = vIn->uvcoords;

	return vOut;
}

BasicColorOut ScreenPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	const glm::vec3 color = glm::vec3(uni->screen->sample(vOut->uvCoords));
	const float scanline = 0.9f + 0.1f * std::sin(vOut->uvCoords.y * static_cast<float>(uni->screen->getSize().y) * kPi);

	return BasicColorOut{ glm::vec4(color * scanline, 1.0f) };
}

GBufferPipeline::VOutput GBufferPipeline::vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni)
{
	VOutput vOut{};
	vOut.position = instance->modelViewProjectionMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.worldPosition = instance->modelMatrix * glm::vec4(vIn->position, 1.0f);
	vOut.worldNormal = instance->modelMatrix * glm::vec4(vIn->normal, 0.0f);

	return vOut;
}

GBufferOut GBufferPipeline::fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw)
{
	const float checker = std::fmod(std::floor(vOut->worldPosition.x) + std::floor(vOut->worldPosition.z), 2.0f) != 0.0f ? 1.0f : 0.0f;
	const float albedoScale = 1.0f - instance->checkerStrength * checker;

	return GBufferOut{
		.albedo = glm::vec4(glm::vec3(instance->color) * albedoScale, instance->specular),
		.normal = glm::vec4(glm::normalize(vOut->worldNormal), instance->shininess)
	};
}

DeferredLightingPipeline::VOutput DeferredLightingPipeline::vertexShader(const VInput* vIn, const Uniform* uni)
{
	return fullscreenVertex(vIn);
}

BasicColorOut DeferredLightingPipeline::fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw)
{
	constexpr glm::vec3 skyColor(0.42f, 0.58f, 0.82f);
	constexpr glm::vec3 groundColor(0.12f, 0.1f, 0.09f);

	const glm::uvec2 texel = glm::uvec2(vOut->position.x, vOut->position.y);
	const float depth = uni->depth->at(texel).x;
	if (depth > 1.0f)
		return BasicColorOut{ glm::vec4(skyColor, 1.0f) };

	const glm::vec2 size = glm::vec2(uni->depth->getSize());
	const glm::vec2 ndcXY = (glm::vec2(texel) + 0.5f) / size * 2.0f - 1.0f;
	const glm::vec4 worldH = uni->inverseViewProjection * glm::vec4(ndcXY.x, -ndcXY.y, depth, 1.0f);
	const glm::vec3 position = glm::vec3(worldH) / worldH.w;

	const glm::vec4 albedoSample = ShaderUtils::srgb8ToLinear(glm::vec4(uni->albedo->at(texel)));
	const glm::vec4 normalSample = uni->normal->at(texel);
	const glm::vec3 albedo = glm::vec3(albedoSample);
	const float specular = albedoSample.a;
	const glm::vec3 normal = glm::vec3(normalSample);
	const float shininess = normalSample.a;
	const glm::vec3 view = glm::normalize(uni->cameraPosition - position);

	glm::vec3 color = glm::mix(groundColor, skyColor, normal.y * 0.5f + 0.5f) * albedo * uni->ambientMult;

	for (const PointLight& light : uni->lights)
	{
		const glm::vec3 toLight = glm::vec3(light.positionRadius) - position;
		const float radius = light.positionRadius.w;
		const float distanceSquared = glm::dot(toLight, toLight);
		if (distanceSquared >= radius * radius)
			continue;

		const float falloff = 1.0f - distanceSquared / (radius * radius);
		const glm::vec3 lightDirection = toLight / std::sqrt(distanceSquared);
		const float diffuse = glm::max(glm::dot(normal, lightDirection), 0.0f);
		const float highlight = diffuse > 0.0f ? specular * std::pow(glm::max(glm::dot(normal, glm::normalize(lightDirection + view)), 0.0f), shininess) : 0.0f;

		color += glm::vec3(light.color) * (falloff * falloff) * (albedo * diffuse + highlight);
	}

	return BasicColorOut{ glm::vec4(color, 1.0f) };
}

NeonScene::NeonScene(CommandBuffer& commandBuffer)
	: recording(commandBuffer.registerPipeline<EmissivePipeline>(makeState(PipelineState::CullMode::None, true, true, true)))
{
	makeCubeMesh(cubeVertices, cubeIndices);
	makeTorusKnotMesh(torusVertices, torusIndices, 192, 24, 0.3f, 2.2f);

	uniform.lightDirection = kLightDirection;

	recording.reserve(2, 1);
}

const char* NeonScene::name()
{
	return "neon";
}

CameraSetup NeonScene::cameraSetup()
{
	return { .position = { 0.0f, 0.0f, 16.0f }, .direction = { 0.0f, 0.0f, -1.0f }, .fov = 60.0f };
}

void NeonScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState clearState{ .colors = { glm::vec4(0.004f, 0.004f, 0.012f, 1.0f) }, .depth = kInfinity };
	constexpr glm::vec4 colors[6] = {
		{ 1.0f, 0.9f, 0.2f, 1.0f }, { 1.0f, 0.3f, 0.8f, 1.0f }, { 0.2f, 1.0f, 0.6f, 1.0f },
		{ 0.3f, 0.8f, 1.0f, 1.0f }, { 1.0f, 0.5f, 0.2f, 1.0f }, { 0.7f, 0.4f, 1.0f, 1.0f }
	};

	recording.clear();

	uniform.viewProjectionMatrix = camera.getVPMatrix();

	glm::mat4 knotModel = glm::rotate(glm::mat4(1.0f), glm::radians(time * 7.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	knotInstance = { .modelMatrix = knotModel, .color = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f) };

	for (uint32_t i = 0; i < kCubeCount; ++i)
	{
		const float angle = glm::radians(time * 5.0f) + static_cast<float>(i) / 6.0f * 2.0f * kPi;
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(std::cos(angle) * 9.0f, std::sin(angle * 2.0f) * 3.0f, std::sin(angle) * 4.0f));
		model = glm::rotate(model, glm::radians(time * 9.0f), glm::vec3(1.0f, 1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(0.8f));
		cubeInstances[i] = { .modelMatrix = model, .color = colors[i] };
	}

	recording.bindUniform(uniform);
	recording.drawIndexedInstanced(torusVertices, torusIndices, std::span<const EmissivePipeline::InstanceInput>(&knotInstance, 1));
	recording.drawIndexedInstanced(cubeVertices, cubeIndices, cubeInstances);

	recording.commit(commandBuffer, framebuffer, &depthBuffer, clearState);
}

ShadowScene::ShadowScene(CommandBuffer& commandBuffer)
	: shadowMap(glm::uvec2(kShadowMapSize), glm::vec1(kInfinity)),
	  casterRecording(commandBuffer.registerPipeline<ShadowCasterPipeline>(makeState(PipelineState::CullMode::None, true, true, false))),
	  litRecording(commandBuffer.registerPipeline<ShadowedPipeline>(makeState(PipelineState::CullMode::None, true, true, true)))
{
	makeCubeMesh(cubeVertices, cubeIndices);
	makeTorusKnotMesh(torusVertices, torusIndices, 192, 24, 0.3f, 1.1f);
	makeGroundMesh(groundVertices, groundIndices, 20.0f);

	litUniform.shadowMap = &shadowMap;
	litUniform.ambientMult = 0.18f;

	casterRecording.reserve(2, 1);
	litRecording.reserve(3, 1);
}

const char* ShadowScene::name()
{
	return "shadow";
}

CameraSetup ShadowScene::cameraSetup()
{
	return { .position = { 0.0f, 9.0f, 17.0f }, .direction = glm::normalize(glm::vec3(0.0f, -9.0f, -17.0f)), .fov = 60.0f };
}

void ShadowScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState shadowClear{ .depth = kInfinity };
	constexpr ClearState sceneClear{ .colors = { glm::vec4(0.42f, 0.58f, 0.82f, 1.0f) }, .depth = kInfinity };
	constexpr float lightDistance = 30.0f;
	constexpr float lightExtent = 20.0f;

	const float lightAngle = glm::radians(time * 4.0f);
	const glm::vec3 lightDirection = glm::normalize(glm::vec3(std::cos(lightAngle) * 0.55f, -1.0f, std::sin(lightAngle) * 0.55f));
	const glm::mat4 lightView = glm::lookAt(-lightDirection * lightDistance, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 lightProjection = glm::ortho(-lightExtent, lightExtent, -lightExtent, lightExtent, 1.0f, 2.0f * lightDistance);
	const glm::mat4 lightViewProjection = lightProjection * lightView;
	const glm::mat4 viewProjection = camera.getVPMatrix();

	casterRecording.clear();
	litRecording.clear();

	litUniform.lightDirection = lightDirection;
	litUniform.normalOffset = 4.0f * lightExtent / static_cast<float>(kShadowMapSize);

	const auto casterInstance = [&](const glm::mat4& model) -> ShadowCasterPipeline::InstanceInput
	{
		return { .lightMatrix = lightViewProjection * model };
	};

	const auto litInstance = [&](const glm::mat4& model, const glm::vec4& color, const float checkerStrength) -> ShadowedPipeline::InstanceInput
	{
		return {
			.modelViewProjectionMatrix = viewProjection * model,
			.modelMatrix = model,
			.lightMatrix = lightViewProjection * model,
			.color = color,
			.checkerStrength = checkerStrength,
		};
	};

	glm::mat4 knotModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.8f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 7.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	casterKnotInstance = casterInstance(knotModel);
	litKnotInstance = litInstance(knotModel, glm::vec4(0.9f, 0.55f, 0.2f, 1.0f), 0.0f);

	const auto setCube = [&](const uint32_t index, const glm::mat4& model, const glm::vec4& color)
	{
		casterCubeInstances[index] = casterInstance(model);
		litCubeInstances[index] = litInstance(model, color, 0.0f);
	};

	const glm::vec3 cubePositions[4] = { { -6.0f, -1.0f, 2.0f }, { 6.0f, -1.0f, 3.0f }, { -4.0f, -1.0f, -5.0f }, { 5.0f, -1.0f, -4.0f } };
	for (uint32_t i = 0; i < 4; ++i)
	{
		const glm::mat4 model = glm::rotate(glm::translate(glm::mat4(1.0f), cubePositions[i]), glm::radians(time * 4.0f + static_cast<float>(i) * 40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		setCube(i, model, glm::vec4(0.35f + 0.15f * static_cast<float>(i), 0.6f, 0.9f - 0.15f * static_cast<float>(i), 1.0f));
	}

	const glm::mat4 floatingModel = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.5f, -6.0f)), glm::radians(time * 6.0f), glm::vec3(1.0f, 1.0f, 0.0f));
	setCube(4, floatingModel, glm::vec4(0.8f, 0.3f, 0.35f, 1.0f));

	litGroundInstance = litInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f)), glm::vec4(0.8f, 0.82f, 0.78f, 1.0f), 0.35f);

	casterRecording.bindUniform(casterUniform);
	casterRecording.drawIndexedInstanced(torusVertices, torusIndices, std::span<const ShadowCasterPipeline::InstanceInput>(&casterKnotInstance, 1));
	casterRecording.drawIndexedInstanced(cubeVertices, cubeIndices, casterCubeInstances);

	litRecording.bindUniform(litUniform);
	litRecording.drawIndexedInstanced(torusVertices, torusIndices, std::span<const ShadowedPipeline::InstanceInput>(&litKnotInstance, 1));
	litRecording.drawIndexedInstanced(cubeVertices, cubeIndices, litCubeInstances);
	litRecording.drawIndexedInstanced(groundVertices, groundIndices, std::span<const ShadowedPipeline::InstanceInput>(&litGroundInstance, 1));

	casterRecording.commit(commandBuffer, shadowMap, shadowClear, glm::uvec2(kShadowMapSize));
	litRecording.commit(commandBuffer, framebuffer, &depthBuffer, sceneClear);
}

MonitorScene::MonitorScene(CommandBuffer& commandBuffer)
	: texture("../texture.png"),
	  monitorColor(glm::uvec2(kMonitorSize), glm::u8vec4(0, 0, 0, 255)),
	  monitorDepth(glm::uvec2(kMonitorSize), glm::vec1(kInfinity)),
	  monitorRecording(commandBuffer.registerPipeline<ColorPipeline>(makeState(PipelineState::CullMode::Back, true, true, true))),
	  roomRecording(commandBuffer.registerPipeline<InstancedColorPipeline>(makeState(PipelineState::CullMode::Back, true, true, true))),
	  screenRecording(commandBuffer.registerPipeline<ScreenPipeline>(makeState(PipelineState::CullMode::None, true, true, true)))
{
	makeCubeMesh(cubeVertices, cubeIndices);
	makeTorusKnotMesh(torusVertices, torusIndices, 192, 24, 0.3f, 1.0f);
	makeScreenMesh(screenVertices, screenIndices, 3.2f);

	texture.setSampler(TRILINEAR, REPEAT);

	monitorColor.setFormat(SRGB);
	monitorColor.setSampler(BILINEAR, CLAMP);

	uniform.tex = &texture;
	uniform.lightDirection = kLightDirection;
	uniform.ambientMult = 0.4f;

	roomUniform.tex = &texture;
	roomUniform.lightDirection = kLightDirection;
	roomUniform.ambientMult = 0.4f;

	screenUniform.screen = &monitorColor;

	monitorRecording.reserve(2, 2);
	roomRecording.reserve(1, 1);
	screenRecording.reserve(1, 1);
}

const char* MonitorScene::name()
{
	return "monitor";
}

CameraSetup MonitorScene::cameraSetup()
{
	return { .position = { 0.0f, 0.0f, 9.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
}

void MonitorScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState monitorClear{ .colors = { glm::vec4(0.02f, 0.12f, 0.18f, 1.0f) }, .depth = kInfinity };
	constexpr ClearState roomClear{ .colors = { glm::vec4(0.01f, 0.01f, 0.02f, 1.0f) }, .depth = kInfinity };

	const glm::mat4 monitorView = glm::lookAt(glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 monitorViewProjection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) * monitorView;

	monitorRecording.clear();
	roomRecording.clear();
	screenRecording.clear();

	const auto drawMonitorObject = [&](const std::vector<ColorPipeline::VInput>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& model, const glm::vec4& color)
	{
		uniform.modelViewProjectionMatrix = monitorViewProjection * model;
		uniform.normalMatrix = glm::transpose(glm::inverse(model));
		uniform.color = color;

		monitorRecording.bindUniform(uniform);
		monitorRecording.drawIndexed(vertices, indices);
	};

	glm::mat4 knotModel = glm::rotate(glm::mat4(1.0f), glm::radians(time * 7.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	drawMonitorObject(torusVertices, torusIndices, knotModel, glm::vec4(1.0f, 0.85f, 0.6f, 1.0f));

	const glm::mat4 orbitModel = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(std::cos(time * 0.2f) * 5.5f, 0.0f, std::sin(time * 0.2f) * 5.5f)), glm::vec3(0.7f));
	drawMonitorObject(cubeVertices, cubeIndices, orbitModel, glm::vec4(1.0f, 0.7f, 0.4f, 1.0f));

	const glm::mat4 viewProjection = camera.getVPMatrix();
	for (uint32_t i = 0; i < kRoomCubeCount; ++i)
	{
		const float angle = static_cast<float>(i) / 10.0f * 2.0f * kPi;
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(std::cos(angle) * 8.0f, std::sin(angle) * 4.5f, -3.0f));
		model = glm::rotate(model, glm::radians(time * 5.0f + static_cast<float>(i) * 30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::rotate(model, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		model = glm::scale(model, glm::vec3(0.6f));
		roomInstances[i] = {
			.modelViewProjectionMatrix = viewProjection * model,
			.normalMatrix = glm::transpose(glm::inverse(model)),
			.color = glm::vec4(static_cast<float>(i) / 10.0f, 0.5f, 1.0f - static_cast<float>(i) / 10.0f, 1.0f),
		};
	}

	roomRecording.bindUniform(roomUniform);
	roomRecording.drawIndexedInstanced(cubeVertices, cubeIndices, roomInstances);

	const glm::mat4 screenModel = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f)), std::sin(time * 0.1f) * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
	screenUniform.modelViewProjectionMatrix = viewProjection * screenModel;
	screenRecording.bindUniform(screenUniform);
	screenRecording.drawIndexed(screenVertices, screenIndices);

	monitorRecording.commit(commandBuffer, monitorColor, &monitorDepth, monitorClear, glm::uvec2(kMonitorSize));
	roomRecording.commit(commandBuffer, framebuffer, &depthBuffer, roomClear);
	screenRecording.commit(commandBuffer, framebuffer, &depthBuffer);
}

DeferredScene::DeferredScene(CommandBuffer& commandBuffer)
	: geometryRecording(commandBuffer.registerPipeline<GBufferPipeline>(makeState(PipelineState::CullMode::None, true, true, true))),
	  lightingRecording(commandBuffer.registerPipeline<DeferredLightingPipeline>(fullscreenState())),
	  markerRecording(commandBuffer.registerPipeline<EmissivePipeline>(makeState(PipelineState::CullMode::None, true, true, true)))
{
	makeCubeMesh(cubeVertices, cubeIndices);
	makeTorusKnotMesh(torusVertices, torusIndices, 192, 24, 0.3f, 1.1f);
	makeGroundMesh(groundVertices, groundIndices, 20.0f);

	lightingUniform.ambientMult = 0.12f;
	markerUniform.lightDirection = kLightDirection;

	geometryRecording.reserve(3, 1);
	lightingRecording.reserve(1, 1);
	markerRecording.reserve(1, 1);
}

const char* DeferredScene::name()
{
	return "deferred";
}

CameraSetup DeferredScene::cameraSetup()
{
	return { .position = { 0.0f, 9.0f, 17.0f }, .direction = glm::normalize(glm::vec3(0.0f, -9.0f, -17.0f)), .fov = 60.0f };
}

void DeferredScene::record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	constexpr ClearState geometryClear{ .colors = { glm::vec4(0.0f), glm::vec4(0.0f) }, .depth = kInfinity };
	constexpr float lightRadius = 7.0f;
	constexpr float lightIntensity = 0.85f;

	const glm::uvec2 targetSize = depthBuffer.getSize();
	if (!albedoTarget || albedoTarget->getSize() != targetSize)
	{
		albedoTarget.emplace(targetSize, glm::u8vec4(0, 0, 0, 255));
		albedoTarget->setFormat(SRGB);
		normalTarget.emplace(targetSize, glm::vec4(0.0f));
	}

	const glm::mat4 viewProjection = camera.getVPMatrix();

	geometryRecording.clear();
	lightingRecording.clear();
	markerRecording.clear();

	const auto geometryInstance = [&](const glm::mat4& model, const glm::vec4& color, const float specular, const float shininess, const float checkerStrength) -> GBufferPipeline::InstanceInput
	{
		return {
			.modelViewProjectionMatrix = viewProjection * model,
			.modelMatrix = model,
			.color = color,
			.specular = specular,
			.shininess = shininess,
			.checkerStrength = checkerStrength,
		};
	};

	glm::mat4 knotModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.8f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 7.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	knotModel = glm::rotate(knotModel, glm::radians(time * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	knotInstance = geometryInstance(knotModel, glm::vec4(0.9f, 0.55f, 0.2f, 1.0f), 0.9f, 48.0f, 0.0f);

	constexpr glm::vec3 cubePositions[4] = { { -6.0f, -1.0f, 2.0f }, { 6.0f, -1.0f, 3.0f }, { -4.0f, -1.0f, -5.0f }, { 5.0f, -1.0f, -4.0f } };
	for (uint32_t i = 0; i < 4; ++i)
	{
		const glm::mat4 model = glm::rotate(glm::translate(glm::mat4(1.0f), cubePositions[i]), glm::radians(time * 4.0f + static_cast<float>(i) * 40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		cubeInstances[i] = geometryInstance(model, glm::vec4(0.35f + 0.15f * static_cast<float>(i), 0.6f, 0.9f - 0.15f * static_cast<float>(i), 1.0f), 0.4f, 16.0f, 0.0f);
	}

	const glm::mat4 floatingModel = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.5f, -6.0f)), glm::radians(time * 6.0f), glm::vec3(1.0f, 1.0f, 0.0f));
	cubeInstances[4] = geometryInstance(floatingModel, glm::vec4(0.8f, 0.3f, 0.35f, 1.0f), 0.6f, 24.0f, 0.0f);

	groundInstance = geometryInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f)), glm::vec4(0.8f, 0.82f, 0.78f, 1.0f), 0.1f, 12.0f, 0.35f);

	geometryRecording.bindUniform(geometryUniform);
	geometryRecording.drawIndexedInstanced(torusVertices, torusIndices, std::span<const GBufferPipeline::InstanceInput>(&knotInstance, 1));
	geometryRecording.drawIndexedInstanced(cubeVertices, cubeIndices, cubeInstances);
	geometryRecording.drawIndexedInstanced(groundVertices, groundIndices, std::span<const GBufferPipeline::InstanceInput>(&groundInstance, 1));

	lightingUniform.inverseViewProjection = glm::inverse(viewProjection);
	lightingUniform.cameraPosition = camera.getPosition();
	lightingUniform.albedo = &*albedoTarget;
	lightingUniform.normal = &*normalTarget;
	lightingUniform.depth = &depthBuffer;

	for (uint32_t i = 0; i < kDeferredLightCount; ++i)
	{
		const float fraction = static_cast<float>(i) / static_cast<float>(kDeferredLightCount);
		const float direction = (i % 2 == 0) ? 1.0f : -1.0f;
		const float angle = glm::radians(time * 6.0f) * direction * (0.6f + fraction) + fraction * 2.0f * kPi * 3.0f;
		const float orbit = 4.0f + 11.0f * std::fmod(fraction * 7.0f, 1.0f);
		const glm::vec3 position(std::cos(angle) * orbit, 0.5f + 1.5f * std::sin(angle * 1.7f + fraction * 9.0f), std::sin(angle) * orbit);
		const glm::vec3 color = glm::clamp(glm::abs(glm::fract(fraction * 5.0f + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);

		lightingUniform.lights[i] = { .positionRadius = glm::vec4(position, lightRadius), .color = glm::vec4(color * lightIntensity, 1.0f) };

		markerInstances[i] = {
			.modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(0.12f)),
			.color = glm::vec4(glm::mix(color, glm::vec3(1.0f), 0.35f), 1.0f),
		};
	}

	markerUniform.viewProjectionMatrix = viewProjection;
	markerRecording.bindUniform(markerUniform);
	markerRecording.drawIndexedInstanced(cubeVertices, cubeIndices, markerInstances);

	lightingRecording.bindUniform(lightingUniform);
	lightingRecording.draw(kFullscreenTriangle);

	geometryRecording.commit(commandBuffer, Attachments{ *albedoTarget, *normalTarget }, &depthBuffer, geometryClear, targetSize);
	lightingRecording.commit(commandBuffer, framebuffer);
	markerRecording.commit(commandBuffer, framebuffer, &depthBuffer);
}
