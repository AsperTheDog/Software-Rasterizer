#pragma once
#include <glm.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "scenes.hpp"

struct FullscreenVertex
{
	glm::vec2 position;
};

struct FullscreenVOut : VOutBase
{
	glm::vec2 uv;
};

extern const std::array<FullscreenVertex, 3> kFullscreenTriangle;

PipelineState fullscreenState();
Texture<glm::u8vec4>& ensureTarget(std::optional<Texture<glm::u8vec4>>& target, glm::uvec2 size);

class PostFxPipeline
{
public:
	struct UniformStruct
	{
		Texture<glm::u8vec4>* source;
		float aberration;
		float vignette;
	};

	typedef UniformStruct Uniform;
	typedef FullscreenVertex VInput;
	typedef FullscreenVOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class BrightPassPipeline
{
public:
	struct UniformStruct
	{
		Texture<glm::u8vec4>* source;
		glm::vec2 texelSize;
		float threshold;
	};

	typedef UniformStruct Uniform;
	typedef FullscreenVertex VInput;
	typedef FullscreenVOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class BlurPipeline
{
public:
	struct UniformStruct
	{
		Texture<glm::u8vec4>* source;
		glm::vec2 step;
	};

	typedef UniformStruct Uniform;
	typedef FullscreenVertex VInput;
	typedef FullscreenVOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class CompositePipeline
{
public:
	struct UniformStruct
	{
		Texture<glm::u8vec4>* scene;
		Texture<glm::u8vec4>* bloom;
		float intensity;
	};

	typedef UniformStruct Uniform;
	typedef FullscreenVertex VInput;
	typedef FullscreenVOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class ShadowCasterPipeline
{
public:
	struct UniformStruct
	{
	};

	struct InstanceStruct
	{
		glm::mat4 lightMatrix;
	};

	typedef UniformStruct Uniform;
	typedef InstanceStruct InstanceInput;
	typedef ColorPipeline::VIn VInput;
	typedef VOutBase VOutput;

	static VOutput vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni);
};

class ShadowedPipeline
{
public:
	struct UniformStruct
	{
		const Texture<glm::vec1>* shadowMap;
		glm::vec3 lightDirection;
		float ambientMult;
		float normalOffset;
	};

	struct InstanceStruct
	{
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 lightMatrix;
		glm::vec4 color;
		float checkerStrength;
	};

	struct VOut : VOutBase
	{
		glm::vec3 worldPosition;
		glm::vec3 worldNormal;
		glm::vec4 lightPosition;
	};

	typedef UniformStruct Uniform;
	typedef InstanceStruct InstanceInput;
	typedef ColorPipeline::VIn VInput;
	typedef VOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw);
};

class ScreenPipeline
{
public:
	struct UniformStruct
	{
		Texture<glm::u8vec4>* screen;
		glm::mat4 modelViewProjectionMatrix;
	};

	struct VOut : VOutBase
	{
		glm::vec2 uvCoords;
	};

	typedef UniformStruct Uniform;
	typedef ColorPipeline::VIn VInput;
	typedef VOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class EmissivePipeline
{
public:
	struct UniformStruct
	{
		glm::mat4 viewProjectionMatrix;
		glm::vec3 lightDirection;
	};

	struct InstanceStruct
	{
		glm::mat4 modelMatrix;
		glm::vec4 color;
	};

	struct VOut : VOutBase
	{
		glm::vec3 normal;
	};

	typedef UniformStruct Uniform;
	typedef InstanceStruct InstanceInput;
	typedef ColorPipeline::VIn VInput;
	typedef VOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw);
};

struct GBufferOut
{
	glm::vec4 albedo;
	glm::vec4 normal;
};

class GBufferPipeline
{
public:
	struct UniformStruct
	{
	};

	struct InstanceStruct
	{
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 modelMatrix;
		glm::vec4 color;
		float specular;
		float shininess;
		float checkerStrength;
	};

	struct VOut : VOutBase
	{
		glm::vec3 worldPosition;
		glm::vec3 worldNormal;
	};

	typedef UniformStruct Uniform;
	typedef InstanceStruct InstanceInput;
	typedef ColorPipeline::VInput VInput;
	typedef VOut VOutput;
	typedef TargetList<glm::u8vec4, glm::vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const InstanceInput* instance, const Uniform* uni);
	static GBufferOut fragmentShader(const VOutput* vOut, const InstanceInput* instance, const Uniform* uni, const float tpw);
};

inline constexpr uint32_t kDeferredLightCount = 32;

struct PointLight
{
	glm::vec4 positionRadius;
	glm::vec4 color;
};

class DeferredLightingPipeline
{
public:
	struct UniformStruct
	{
		glm::mat4 inverseViewProjection;
		glm::vec3 cameraPosition;
		float ambientMult;
		const Texture<glm::u8vec4>* albedo;
		const Texture<glm::vec4>* normal;
		const Texture<glm::vec1>* depth;
		std::array<PointLight, kDeferredLightCount> lights;
	};

	typedef UniformStruct Uniform;
	typedef FullscreenVertex VInput;
	typedef FullscreenVOut VOutput;
	typedef TargetList<glm::u8vec4> Targets;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static BasicColorOut fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class NeonScene
{
public:
	static constexpr uint32_t kCubeCount = 6;

	explicit NeonScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	std::vector<ColorPipeline::VInput> torusVertices;
	std::vector<uint32_t> torusIndices;
	EmissivePipeline::Uniform uniform{};
	EmissivePipeline::InstanceInput knotInstance{};
	std::array<EmissivePipeline::InstanceInput, kCubeCount> cubeInstances{};
	CommandBufferRecording<EmissivePipeline> recording;
};

class ShadowScene
{
public:
	static constexpr uint32_t kShadowMapSize = 1024;
	static constexpr uint32_t kCubeCount = 5;

	explicit ShadowScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	std::vector<ColorPipeline::VInput> torusVertices;
	std::vector<uint32_t> torusIndices;
	std::vector<ColorPipeline::VInput> groundVertices;
	std::vector<uint32_t> groundIndices;
	Texture<glm::vec1> shadowMap;
	ShadowCasterPipeline::Uniform casterUniform{};
	ShadowCasterPipeline::InstanceInput casterKnotInstance{};
	std::array<ShadowCasterPipeline::InstanceInput, kCubeCount> casterCubeInstances{};
	ShadowedPipeline::Uniform litUniform{};
	ShadowedPipeline::InstanceInput litKnotInstance{};
	std::array<ShadowedPipeline::InstanceInput, kCubeCount> litCubeInstances{};
	ShadowedPipeline::InstanceInput litGroundInstance{};
	CommandBufferRecording<ShadowCasterPipeline> casterRecording;
	CommandBufferRecording<ShadowedPipeline> litRecording;
};

class MonitorScene
{
public:
	static constexpr uint32_t kMonitorSize = 256;
	static constexpr uint32_t kRoomCubeCount = 10;

	explicit MonitorScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	std::vector<ColorPipeline::VInput> torusVertices;
	std::vector<uint32_t> torusIndices;
	std::vector<ColorPipeline::VInput> screenVertices;
	std::vector<uint32_t> screenIndices;
	MipTexture<glm::u8vec4> texture;
	Texture<glm::u8vec4> monitorColor;
	Texture<glm::vec1> monitorDepth;
	ColorPipeline::Uniform uniform{};
	InstancedColorPipeline::Uniform roomUniform{};
	std::array<InstancedColorPipeline::InstanceInput, kRoomCubeCount> roomInstances{};
	ScreenPipeline::Uniform screenUniform{};
	CommandBufferRecording<ColorPipeline> monitorRecording;
	CommandBufferRecording<InstancedColorPipeline> roomRecording;
	CommandBufferRecording<ScreenPipeline> screenRecording;
};

class DeferredScene
{
public:
	static constexpr uint32_t kCubeCount = 5;

	explicit DeferredScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	std::vector<ColorPipeline::VInput> torusVertices;
	std::vector<uint32_t> torusIndices;
	std::vector<ColorPipeline::VInput> groundVertices;
	std::vector<uint32_t> groundIndices;
	std::optional<Texture<glm::u8vec4>> albedoTarget;
	std::optional<Texture<glm::vec4>> normalTarget;
	GBufferPipeline::Uniform geometryUniform{};
	GBufferPipeline::InstanceInput knotInstance{};
	std::array<GBufferPipeline::InstanceInput, kCubeCount> cubeInstances{};
	GBufferPipeline::InstanceInput groundInstance{};
	DeferredLightingPipeline::Uniform lightingUniform{};
	EmissivePipeline::Uniform markerUniform{};
	std::array<EmissivePipeline::InstanceInput, kDeferredLightCount> markerInstances{};
	CommandBufferRecording<GBufferPipeline> geometryRecording;
	CommandBufferRecording<DeferredLightingPipeline> lightingRecording;
	CommandBufferRecording<EmissivePipeline> markerRecording;
};

template<Scene S>
class PostFxScene
{
public:
	explicit PostFxScene(CommandBuffer& commandBuffer)
		: inner(commandBuffer),
		  recording(commandBuffer.registerPipeline<PostFxPipeline>(fullscreenState()))
	{
		uniform.aberration = 0.012f;
		uniform.vignette = 1.2f;

		recording.reserve(1, 1);
	}

	static const char* name()
	{
		static const std::string label = std::string(S::name()) + "+postfx";
		return label.c_str();
	}

	static CameraSetup cameraSetup()
	{
		return S::cameraSetup();
	}

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
	{
		Texture<glm::u8vec4>& sceneColor = ensureTarget(sceneTarget, framebuffer.getSize());

		inner.record(commandBuffer, sceneColor, depthBuffer, camera, time);

		uniform.source = &sceneColor;

		recording.clear();
		recording.bindUniform(uniform);
		recording.draw(kFullscreenTriangle);
		recording.commit(commandBuffer, framebuffer);
	}

private:
	S inner;
	std::optional<Texture<glm::u8vec4>> sceneTarget;
	PostFxPipeline::Uniform uniform{};
	CommandBufferRecording<PostFxPipeline> recording;
};

template<Scene S>
class BloomScene
{
public:
	static constexpr uint32_t kBloomDownsample = 4;

	explicit BloomScene(CommandBuffer& commandBuffer)
		: inner(commandBuffer),
		  brightRecording(commandBuffer.registerPipeline<BrightPassPipeline>(fullscreenState())),
		  blurHorizontalRecording(commandBuffer.registerPipeline<BlurPipeline>(fullscreenState())),
		  blurVerticalRecording(commandBuffer.registerPipeline<BlurPipeline>(fullscreenState())),
		  compositeRecording(commandBuffer.registerPipeline<CompositePipeline>(fullscreenState()))
	{
		brightUniform.threshold = 0.6f;
		compositeUniform.intensity = 1.0f;

		brightRecording.reserve(1, 1);
		blurHorizontalRecording.reserve(1, 1);
		blurVerticalRecording.reserve(1, 1);
		compositeRecording.reserve(1, 1);
	}

	static const char* name()
	{
		static const std::string label = std::string(S::name()) + "+bloom";
		return label.c_str();
	}

	static CameraSetup cameraSetup()
	{
		return S::cameraSetup();
	}

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
	{
		const glm::uvec2 fullSize = framebuffer.getSize();
		const glm::uvec2 smallSize = (fullSize + glm::uvec2(kBloomDownsample - 1u)) / kBloomDownsample;

		Texture<glm::u8vec4>& sceneColor = ensureTarget(sceneTarget, fullSize);
		Texture<glm::u8vec4>& ping = ensureTarget(pingTarget, smallSize);
		Texture<glm::u8vec4>& pong = ensureTarget(pongTarget, smallSize);

		inner.record(commandBuffer, sceneColor, depthBuffer, camera, time);

		brightUniform.source = &sceneColor;
		brightUniform.texelSize = 1.0f / glm::vec2(fullSize);
		brightRecording.clear();
		brightRecording.bindUniform(brightUniform);
		brightRecording.draw(kFullscreenTriangle);
		brightRecording.commit(commandBuffer, ping, nullptr, {}, smallSize);

		blurHorizontalUniform.source = &ping;
		blurHorizontalUniform.step = glm::vec2(1.0f / static_cast<float>(smallSize.x), 0.0f);
		blurHorizontalRecording.clear();
		blurHorizontalRecording.bindUniform(blurHorizontalUniform);
		blurHorizontalRecording.draw(kFullscreenTriangle);
		blurHorizontalRecording.commit(commandBuffer, pong, nullptr, {}, smallSize);

		blurVerticalUniform.source = &pong;
		blurVerticalUniform.step = glm::vec2(0.0f, 1.0f / static_cast<float>(smallSize.y));
		blurVerticalRecording.clear();
		blurVerticalRecording.bindUniform(blurVerticalUniform);
		blurVerticalRecording.draw(kFullscreenTriangle);
		blurVerticalRecording.commit(commandBuffer, ping, nullptr, {}, smallSize);

		compositeUniform.scene = &sceneColor;
		compositeUniform.bloom = &ping;
		compositeRecording.clear();
		compositeRecording.bindUniform(compositeUniform);
		compositeRecording.draw(kFullscreenTriangle);
		compositeRecording.commit(commandBuffer, framebuffer);
	}

private:
	S inner;
	std::optional<Texture<glm::u8vec4>> sceneTarget;
	std::optional<Texture<glm::u8vec4>> pingTarget;
	std::optional<Texture<glm::u8vec4>> pongTarget;
	BrightPassPipeline::Uniform brightUniform{};
	BlurPipeline::Uniform blurHorizontalUniform{};
	BlurPipeline::Uniform blurVerticalUniform{};
	CompositePipeline::Uniform compositeUniform{};
	CommandBufferRecording<BrightPassPipeline> brightRecording;
	CommandBufferRecording<BlurPipeline> blurHorizontalRecording;
	CommandBufferRecording<BlurPipeline> blurVerticalRecording;
	CommandBufferRecording<CompositePipeline> compositeRecording;
};
