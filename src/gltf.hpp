#pragma once
#include <glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "texture.hpp"

struct GltfVertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uvcoords;
};

struct GltfMaterial
{
	glm::vec4 baseColorFactor{ 1.0f };
	glm::vec3 emissiveFactor{ 0.0f };
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	float occlusionStrength = 1.0f;
	int32_t baseColorTexture = -1;
	int32_t metallicRoughnessTexture = -1;
	int32_t emissiveTexture = -1;
	int32_t occlusionTexture = -1;
	bool doubleSided = false;
	bool alphaMask = false;
	float alphaCutoff = 0.5f;
};

struct GltfPrimitive
{
	std::vector<GltfVertex> vertices;
	std::vector<uint32_t> indices;
	int32_t material = -1;
};

struct GltfModel
{
	std::vector<GltfPrimitive> primitives;
	std::vector<GltfMaterial> materials;
	std::vector<std::unique_ptr<Texture<glm::u8vec4>>> textures;
	glm::vec3 boundsMin{ 0.0f };
	glm::vec3 boundsMax{ 0.0f };
	uint32_t sourceVertexCount = 0;
	uint32_t sourceIndexCount = 0;
	uint32_t sourceTriangleCount = 0;
	uint32_t sourceImageCount = 0;
	float appliedScale = 1.0f;
	bool valid = false;
};

GltfModel loadGltf(const std::string& path);
