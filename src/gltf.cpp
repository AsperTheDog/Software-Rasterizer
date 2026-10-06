#include "gltf.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <tiny_gltf_v3.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

#include <stb_image.h>

namespace
{
	struct AccessorView
	{
		const uint8_t* data = nullptr;
		uint64_t count = 0;
		int32_t stride = 0;
		int32_t componentType = -1;
		int32_t components = 0;
		bool normalized = false;

		[[nodiscard]] bool valid() const
		{
			return data != nullptr && count > 0 && stride > 0 && components > 0
				&& tg3_component_size(componentType) > 0;
		}
	};

	bool readFile(const std::string& path, std::vector<uint8_t>& out)
	{
		std::ifstream file{ path, std::ios::binary | std::ios::ate };

		if (!file.is_open())
			return false;

		const std::streamoff size = file.tellg();

		if (size <= 0)
			return false;

		out.resize(static_cast<size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(out.data()), size);

		return static_cast<size_t>(file.gcount()) == out.size();
	}

	uint32_t base64Value(const char character)
	{
		if (character >= 'A' && character <= 'Z')
			return static_cast<uint32_t>(character - 'A');
		if (character >= 'a' && character <= 'z')
			return static_cast<uint32_t>(character - 'a') + 26u;
		if (character >= '0' && character <= '9')
			return static_cast<uint32_t>(character - '0') + 52u;
		if (character == '+')
			return 62u;
		if (character == '/')
			return 63u;

		return UINT32_MAX;
	}

	std::vector<uint8_t> decodeBase64(const std::string_view text)
	{
		std::vector<uint8_t> out;
		out.reserve(text.size() / 4u * 3u);

		uint32_t accumulator = 0;
		uint32_t bits = 0;

		for (const char character : text)
		{
			const uint32_t value = base64Value(character);

			if (value == UINT32_MAX)
				continue;

			accumulator = (accumulator << 6u) | value;
			bits += 6u;

			if (bits >= 8u)
			{
				bits -= 8u;
				out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
			}
		}

		return out;
	}

	AccessorView makeAccessorView(const tg3_model& source, const int32_t accessorIndex)
	{
		AccessorView view{};

		if (accessorIndex < 0 || std::cmp_greater_equal(accessorIndex, source.accessors_count))
			return view;

		const tg3_accessor& accessor = source.accessors[accessorIndex];

		if (accessor.buffer_view < 0 || std::cmp_greater_equal(accessor.buffer_view, source.buffer_views_count))
			return view;

		const tg3_buffer_view& bufferView = source.buffer_views[accessor.buffer_view];

		if (bufferView.buffer < 0 || std::cmp_greater_equal(bufferView.buffer, source.buffers_count))
			return view;

		const tg3_buffer& buffer = source.buffers[bufferView.buffer];
		const uint64_t offset = bufferView.byte_offset + accessor.byte_offset;

		if (!buffer.data.data || offset >= buffer.data.count)
			return view;

		view.data = buffer.data.data + offset;
		view.count = accessor.count;
		view.stride = tg3_accessor_byte_stride(&accessor, &bufferView);
		view.componentType = accessor.component_type;
		view.components = tg3_num_components(accessor.type);
		view.normalized = accessor.normalized != 0;

		return view;
	}

	float readComponent(const uint8_t* base, const int32_t componentType, const bool normalized)
	{
		switch (componentType)
		{
		case TG3_COMPONENT_TYPE_FLOAT:
		{
			float value = 0.0f;
			std::memcpy(&value, base, sizeof(value));
			return value;
		}
		case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
			return normalized ? static_cast<float>(*base) / 255.0f : static_cast<float>(*base);
		case TG3_COMPONENT_TYPE_BYTE:
		{
			const int8_t value = static_cast<int8_t>(*base);
			return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f) : static_cast<float>(value);
		}
		case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			uint16_t value = 0;
			std::memcpy(&value, base, sizeof(value));
			return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
		}
		case TG3_COMPONENT_TYPE_SHORT:
		{
			int16_t value = 0;
			std::memcpy(&value, base, sizeof(value));
			return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f) : static_cast<float>(value);
		}
		case TG3_COMPONENT_TYPE_UNSIGNED_INT:
		{
			uint32_t value = 0;
			std::memcpy(&value, base, sizeof(value));
			return static_cast<float>(value);
		}
		default:
			return 0.0f;
		}
	}

	glm::vec3 readVec3(const AccessorView& view, const uint64_t index)
	{
		const uint8_t* base = view.data + index * static_cast<uint64_t>(view.stride);
		const int32_t size = tg3_component_size(view.componentType);

		return {
			readComponent(base, view.componentType, view.normalized),
			readComponent(base + size, view.componentType, view.normalized),
			readComponent(base + size * 2, view.componentType, view.normalized)
		};
	}

	glm::vec2 readVec2(const AccessorView& view, const uint64_t index)
	{
		const uint8_t* base = view.data + index * static_cast<uint64_t>(view.stride);
		const int32_t size = tg3_component_size(view.componentType);

		return {
			readComponent(base, view.componentType, view.normalized),
			readComponent(base + size, view.componentType, view.normalized)
		};
	}

	uint32_t readIndex(const AccessorView& view, const uint64_t index)
	{
		const uint8_t* base = view.data + index * static_cast<uint64_t>(view.stride);
		uint32_t value = 0;

		switch (view.componentType)
		{
		case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
			value = *base;
			break;
		case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
			std::memcpy(&value, base, sizeof(uint16_t));
			break;
		case TG3_COMPONENT_TYPE_UNSIGNED_INT:
			std::memcpy(&value, base, sizeof(uint32_t));
			break;
		default:
			break;
		}

		return value;
	}

	glm::mat4 nodeMatrix(const tg3_node& node)
	{
		if (node.has_matrix)
		{
			glm::mat4 matrix{ 1.0f };

			for (uint32_t column = 0; column < 4; ++column)
				for (uint32_t row = 0; row < 4; ++row)
					matrix[column][row] = static_cast<float>(node.matrix[column * 4 + row]);

			return matrix;
		}

		glm::mat4 matrix = glm::translate(glm::mat4{ 1.0f }, glm::vec3{
			static_cast<float>(node.translation[0]),
			static_cast<float>(node.translation[1]),
			static_cast<float>(node.translation[2]) });

		matrix *= glm::mat4_cast(glm::quat{
			static_cast<float>(node.rotation[3]),
			static_cast<float>(node.rotation[0]),
			static_cast<float>(node.rotation[1]),
			static_cast<float>(node.rotation[2]) });

		return glm::scale(matrix, glm::vec3{
			static_cast<float>(node.scale[0]),
			static_cast<float>(node.scale[1]),
			static_cast<float>(node.scale[2]) });
	}

	int32_t findAttribute(const tg3_primitive& primitive, const char* name)
	{
		for (uint32_t i = 0; i < primitive.attributes_count; ++i)
			if (tg3_str_equals_cstr(primitive.attributes[i].key, name))
				return primitive.attributes[i].value;

		return -1;
	}

	int32_t textureImageIndex(const tg3_model& source, const int32_t textureIndex)
	{
		if (textureIndex < 0 || std::cmp_greater_equal(textureIndex, source.textures_count))
			return -1;

		return source.textures[textureIndex].source;
	}

	std::vector<uint8_t> imageBytes(const tg3_model& source, const tg3_image& image, const std::string& directory)
	{
		if (image.image.count > 0 && image.image.data)
			return std::vector<uint8_t>(image.image.data, image.image.data + image.image.count);

		if (image.buffer_view >= 0 && std::cmp_less(image.buffer_view, source.buffer_views_count))
		{
			const tg3_buffer_view& bufferView = source.buffer_views[image.buffer_view];

			if (bufferView.buffer >= 0 && std::cmp_less(bufferView.buffer, source.buffers_count))
			{
				const tg3_buffer& buffer = source.buffers[bufferView.buffer];

				if (buffer.data.data && bufferView.byte_offset + bufferView.byte_length <= buffer.data.count)
					return std::vector<uint8_t>(
						buffer.data.data + bufferView.byte_offset,
						buffer.data.data + bufferView.byte_offset + bufferView.byte_length);
			}

			return {};
		}

		if (image.uri.len > 0 && image.uri.data)
		{
			const std::string_view uri{ image.uri.data, image.uri.len };

			if (tg3_is_data_uri(uri.data(), static_cast<uint32_t>(uri.size())))
			{
				const size_t comma = uri.find(',');
				if (comma == std::string_view::npos)
					return {};

				const std::string_view payload = uri.substr(comma + 1);
				return decodeBase64(payload);
			}

			std::vector<uint8_t> external;
			if (readFile(directory + std::string(uri), external))
				return external;
		}

		return {};
	}
}

GltfModel loadGltf(const std::string& path)
{
	GltfModel model{};

	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
	{
		std::fprintf(stderr, "[gltf] could not read '%s'\n", path.c_str());
		return model;
	}

	tg3_model source{};
	tg3_error_stack errors{};
	tg3_error_stack_init(&errors);

	tg3_parse_options options{};
	tg3_parse_options_init(&options);

	const std::string directory = path.substr(0, path.find_last_of("/\\") + 1);
	const tg3_error_code code = tg3_parse_auto(&source, &errors, bytes.data(), static_cast<uint64_t>(bytes.size()),
		directory.data(), static_cast<uint32_t>(directory.size()), &options);

	if (code != TG3_OK || tg3_errors_has_error(&errors))
	{
		const uint32_t errorCount = tg3_errors_count(&errors);
		std::fprintf(stderr, "[gltf] failed to parse '%s' (%u errors)\n", path.c_str(), errorCount);

		for (uint32_t i = 0; i < errorCount && i < 3; ++i)
		{
			const tg3_error_entry* entry = tg3_errors_get(&errors, i);

			if (entry && entry->message)
				std::fprintf(stderr, "[gltf]   %s\n", entry->message);
		}

		tg3_error_stack_free(&errors);
		tg3_model_free(&source);
		return model;
	}

	std::vector<bool> srgb(source.textures_count, false);

	model.materials.reserve(source.materials_count);

	for (uint32_t i = 0; i < source.materials_count; ++i)
	{
		const tg3_material& material = source.materials[i];
		const tg3_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;

		GltfMaterial out{};
		out.baseColorFactor = glm::vec4(
			static_cast<float>(pbr.base_color_factor[0]),
			static_cast<float>(pbr.base_color_factor[1]),
			static_cast<float>(pbr.base_color_factor[2]),
			static_cast<float>(pbr.base_color_factor[3]));
		out.emissiveFactor = glm::vec3(
			static_cast<float>(material.emissive_factor[0]),
			static_cast<float>(material.emissive_factor[1]),
			static_cast<float>(material.emissive_factor[2]));
		out.metallicFactor = static_cast<float>(pbr.metallic_factor);
		out.roughnessFactor = static_cast<float>(pbr.roughness_factor);
		out.occlusionStrength = static_cast<float>(material.occlusion_texture.strength);
		out.baseColorTexture = pbr.base_color_texture.index;
		out.metallicRoughnessTexture = pbr.metallic_roughness_texture.index;
		out.emissiveTexture = material.emissive_texture.index;
		out.occlusionTexture = material.occlusion_texture.index;
		out.doubleSided = material.double_sided != 0;
		out.alphaMask = tg3_str_equals_cstr(material.alpha_mode, "MASK") != 0;
		out.alphaCutoff = static_cast<float>(material.alpha_cutoff);

		for (const int32_t textureIndex : { out.baseColorTexture, out.emissiveTexture })
			if (textureIndex >= 0 && static_cast<uint32_t>(textureIndex) < srgb.size())
				srgb[textureIndex] = true;

		model.materials.push_back(out);
	}

	const auto appendNode = [&](auto&& self, const int32_t nodeIndex, const glm::mat4& parent) -> void
	{
		if (nodeIndex < 0 || std::cmp_greater_equal(nodeIndex, source.nodes_count))
			return;

		const tg3_node& node = source.nodes[nodeIndex];
		const glm::mat4 world = parent * nodeMatrix(node);

		if (node.mesh >= 0 && std::cmp_less(node.mesh, source.meshes_count))
		{
			const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(world)));
			const tg3_mesh& mesh = source.meshes[node.mesh];

			for (uint32_t p = 0; p < mesh.primitives_count; ++p)
			{
				const tg3_primitive& primitive = mesh.primitives[p];

				if (primitive.mode != TG3_MODE_TRIANGLES)
					continue;

				const AccessorView positions = makeAccessorView(source, findAttribute(primitive, "POSITION"));

				if (!positions.valid())
					continue;

				const AccessorView normals = makeAccessorView(source, findAttribute(primitive, "NORMAL"));
				const AccessorView uvs = makeAccessorView(source, findAttribute(primitive, "TEXCOORD_0"));
				const AccessorView indices = makeAccessorView(source, primitive.indices);

				GltfPrimitive out{};
				out.material = primitive.material;
				out.vertices.reserve(positions.count);

				for (uint64_t v = 0; v < positions.count; ++v)
				{
					GltfVertex vertex{};
					vertex.position = glm::vec3(world * glm::vec4(readVec3(positions, v), 1.0f));
					vertex.normal = normals.valid()
						? normalMatrix * readVec3(normals, v)
						: glm::vec3(0.0f, 1.0f, 0.0f);
					vertex.uvcoords = uvs.valid() ? readVec2(uvs, v) : glm::vec2(0.0f);
					out.vertices.push_back(vertex);
				}

				if (indices.valid())
				{
					out.indices.reserve(indices.count);

					for (uint64_t i = 0; i < indices.count; ++i)
						out.indices.push_back(readIndex(indices, i));
				}
				else
				{
					out.indices.reserve(positions.count);

					for (uint32_t i = 0; i < positions.count; ++i)
						out.indices.push_back(i);
				}

				model.sourceVertexCount += static_cast<uint32_t>(out.vertices.size());
				model.sourceIndexCount += static_cast<uint32_t>(out.indices.size());
				model.sourceTriangleCount += static_cast<uint32_t>(out.indices.size() / 3u);

				model.primitives.push_back(std::move(out));
			}
		}

		for (uint32_t c = 0; c < node.children_count; ++c)
			self(self, node.children[c], world);
	};

	if (source.scenes_count > 0)
	{
		const int32_t sceneIndex = (source.default_scene >= 0 && std::cmp_less(source.default_scene, source.scenes_count))
			? source.default_scene
			: 0;
		const tg3_scene& scene = source.scenes[sceneIndex];

		for (uint32_t i = 0; i < scene.nodes_count; ++i)
			appendNode(appendNode, scene.nodes[i], glm::mat4{ 1.0f });
	}
	else
	{
		for (uint32_t i = 0; i < source.nodes_count; ++i)
			appendNode(appendNode, static_cast<int32_t>(i), glm::mat4{ 1.0f });
	}

	model.textures.reserve(source.textures_count);

	for (uint32_t i = 0; i < source.textures_count; ++i)
	{
		const int32_t imageIndex = textureImageIndex(source, static_cast<int32_t>(i));

		if (imageIndex < 0 || std::cmp_greater_equal(imageIndex, source.images_count))
		{
			model.textures.push_back(nullptr);
			continue;
		}

		const std::vector<uint8_t> encoded = imageBytes(source, source.images[imageIndex], directory);

		model.sourceImageCount += encoded.empty() ? 0u : 1u;

		int32_t width = 0;
		int32_t height = 0;
		int32_t channels = 0;
		stbi_uc* decoded = encoded.empty()
			? nullptr
			: stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);

		if (!decoded || width <= 0 || height <= 0)
		{
			stbi_image_free(decoded);
			model.textures.push_back(nullptr);
			continue;
		}

		auto texture = std::make_unique<Texture<glm::u8vec4>>(
			glm::uvec2(static_cast<uint32_t>(width), static_cast<uint32_t>(height)),
			glm::u8vec4(0, 0, 0, 255), true);

		std::memcpy(texture->data(), decoded, static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(glm::u8vec4));
		stbi_image_free(decoded);

		texture->setFormat(srgb[i] ? SRGB : UNORM);
		texture->setSampler(BILINEAR, REPEAT);

		model.textures.push_back(std::move(texture));
	}

	if (!model.primitives.empty())
	{
		model.boundsMin = glm::vec3(std::numeric_limits<float>::max());
		model.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

		for (const GltfPrimitive& primitive : model.primitives)
			for (const GltfVertex& vertex : primitive.vertices)
			{
				model.boundsMin = glm::min(model.boundsMin, vertex.position);
				model.boundsMax = glm::max(model.boundsMax, vertex.position);
			}

		const glm::vec3 center = (model.boundsMin + model.boundsMax) * 0.5f;
		const glm::vec3 extent = model.boundsMax - model.boundsMin;
		const float maxExtent = std::max({ extent.x, extent.y, extent.z });

		if (maxExtent > 0.0f)
		{
			model.appliedScale = 2.0f / maxExtent;

			for (GltfPrimitive& primitive : model.primitives)
				for (GltfVertex& vertex : primitive.vertices)
					vertex.position = (vertex.position - center) * model.appliedScale;
		}
	}

	tg3_error_stack_free(&errors);
	tg3_model_free(&source);

	model.valid = !model.primitives.empty();

	return model;
}
