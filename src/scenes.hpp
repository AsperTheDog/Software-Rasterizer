#pragma once
#include <glm.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
#include <vector>

#include "camera.hpp"
#include "command_buffer.hpp"
#include "pipeline.hpp"
#include "renderer.hpp"
#include "texture.hpp"

struct CameraSetup
{
	glm::vec3 position;
	glm::vec3 direction;
	float fov = 70.0f;
};

template<typename S>
concept Scene = std::is_constructible_v<S, CommandBuffer&> && requires(S& scene, CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time)
{
	{ S::name() } -> std::convertible_to<const char*>;
	{ S::cameraSetup() } -> std::same_as<CameraSetup>;
	scene.record(commandBuffer, framebuffer, depthBuffer, camera, time);
};

class ColorPipeline
{
public:
	struct UniformStruct
	{
		MipTexture<glm::u8vec4>* tex;
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 normalMatrix;
		glm::vec3 lightDirection;
		glm::vec4 color;
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

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
	static glm::vec2 getUV(const VOutput* vOut);
};

class TranslucentPipeline
{
public:
	struct UniformStruct
	{
		MipTexture<glm::u8vec4>* tex;
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 normalMatrix;
		glm::vec3 lightDirection;
		glm::vec4 color;
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

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
	static glm::vec2 getUV(const VOutput* vOut);
	static glm::vec4 blendShader(const glm::vec4& src, const glm::vec4& dst, const Uniform* uni);
};

class TerrainPipeline
{
public:
	struct UniformStruct
	{
		glm::mat4 modelViewProjectionMatrix;
		glm::mat4 normalMatrix;
		glm::vec3 lightDirection;
		float ambientMult;
	};

	struct VIn
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
	};

	struct VOut : VOutBase
	{
		glm::vec3 normal;
		glm::vec3 color;
	};

	typedef UniformStruct Uniform;
	typedef VIn VInput;
	typedef VOut VOutput;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class SolidPipeline
{
public:
	struct UniformStruct
	{
		glm::mat4 modelViewProjectionMatrix;
		glm::vec4 color;
	};

	struct VIn
	{
		glm::vec3 position;
	};

	struct VOut : VOutBase
	{
	};

	typedef UniformStruct Uniform;
	typedef VIn VInput;
	typedef VOut VOutput;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class PaletteDisplayPipeline
{
public:
	struct UniformStruct
	{
		const glm::vec3* palette;
		uint32_t count;
	};

	struct VIn
	{
		glm::vec3 position;
		glm::vec2 uvcoords;
	};

	struct VOut : VOutBase
	{
		glm::vec2 uvCoords;
	};

	typedef UniformStruct Uniform;
	typedef VIn VInput;
	typedef VOut VOutput;

	static VOutput vertexShader(const VInput* vIn, const Uniform* uni);
	static glm::vec4 fragmentShader(const VOutput* vOut, const Uniform* uni, const float tpw);
};

class PaletteComputePipeline
{
public:
	struct UniformStruct
	{
		glm::vec3* palette;
		uint32_t count;
	};

	typedef UniformStruct Uniform;

	static void computeShader(const ComputeContext& ctx, const Uniform* uni);
};

extern const glm::vec3 kLightDirection;

CameraSetup cubeCameraSetup();
CameraSetup terrainCameraSetup();
CameraSetup torusKnotCameraSetup();
CameraSetup blendCameraSetup();
CameraSetup mipDebugCameraSetup();
CameraSetup stressCameraSetup();
CameraSetup computeCameraSetup();

void makeCubeMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices);
void makeTorusKnotMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, uint32_t segments, uint32_t sides, float radius, float scale);
void makeTerrainMesh(std::vector<TerrainPipeline::VInput>& vertices, std::vector<uint32_t>& indices, uint32_t cells, float extent);
void makeBlendMesh(std::vector<TranslucentPipeline::VInput>& vertices, uint32_t paneCount);
void makeMipDebugMesh(std::vector<ColorPipeline::VInput>& vertices, std::vector<uint32_t>& indices, uint32_t slabCount);
void makeStressMesh(std::vector<SolidPipeline::VInput>& vertices, std::vector<SolidPipeline::Uniform>& uniforms, uint32_t drawCount, uint32_t padFillerCount);
void makeComputePaletteMesh(std::vector<PaletteDisplayPipeline::VInput>& vertices);

class CubesScene
{
public:
	explicit CubesScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> vertices;
	std::vector<uint32_t> indices;
	MipTexture<glm::u8vec4> texture;
	ColorPipeline::Uniform uniform;
	CommandBufferRecording<ColorPipeline> recording;
};

class TerrainScene
{
public:
	explicit TerrainScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<TerrainPipeline::VInput> vertices;
	std::vector<uint32_t> indices;
	TerrainPipeline::Uniform uniform;
	CommandBufferRecording<TerrainPipeline> recording;
};

class TorusKnotScene
{
public:
	TorusKnotScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> vertices;
	std::vector<uint32_t> indices;
	MipTexture<glm::u8vec4> texture;
	ColorPipeline::Uniform uniform;
	CommandBufferRecording<ColorPipeline> recording;
};

class BlendScene
{
public:
	explicit BlendScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	std::vector<TranslucentPipeline::VInput> paneVertices;
	MipTexture<glm::u8vec4> texture;
	ColorPipeline::Uniform opaqueUniform;
	TranslucentPipeline::Uniform paneUniform;
	CommandBufferRecording<ColorPipeline> opaqueRecording;
	CommandBufferRecording<TranslucentPipeline> paneRecording;
};

class MipDebugScene
{
public:
	explicit MipDebugScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<ColorPipeline::VInput> vertices;
	std::vector<uint32_t> indices;
	MipTexture<glm::u8vec4> texture;
	ColorPipeline::Uniform uniform;
	CommandBufferRecording<ColorPipeline> recording;
	bool debugMode = false;
};

class StressScene
{
public:
	static constexpr uint32_t kDrawCount = 4000;
	static constexpr uint32_t kPadFillerCount = 0;

	explicit StressScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<SolidPipeline::VInput> vertices;
	std::vector<SolidPipeline::Uniform> uniforms;
	CommandBufferRecording<SolidPipeline> recording;
};

class ComputeScene
{
public:
	static constexpr uint32_t kPaletteSize = 256;
	static constexpr uint32_t kLocalGroupSize = 64;

	explicit ComputeScene(CommandBuffer& commandBuffer);

	static const char* name();
	static CameraSetup cameraSetup();

	void record(CommandBuffer& commandBuffer, Texture<glm::u8vec4>& framebuffer, Texture<glm::vec1>& depthBuffer, Camera& camera, const float time);

private:
	std::vector<glm::vec3> palette;
	std::vector<PaletteDisplayPipeline::VInput> vertices;
	PaletteComputePipeline::Uniform computeUniform;
	PaletteDisplayPipeline::Uniform displayUniform;
	CommandBufferRecording<PaletteDisplayPipeline> recording;
};
