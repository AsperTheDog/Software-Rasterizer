#pragma once
#include <cstdint>
#include <glm.hpp>
#include <span>

#include "pipeline.hpp"
#include "texture.hpp"

template<Pipeline P>
class CommandBufferRecording;

class CommandBuffer 
{
public:
	struct DrawCallBatchCommand 
	{
		struct Framebuffer
		{
			glm::vec4(*sample)(void* texture, const glm::uvec2& pos);
			void(*write)(void* texture, const glm::vec4& value, const glm::uvec2& pos);

			void* texture;
		};

		struct DrawCallData 
		{
			const void* uniform;
			const void* vertexData;
			const uint32_t* indexData;

			uint32_t vertexCount;
		};

		void(*vertexShader)(void* outVertexOutput, const void* vertexInput, const void* uniform);
		void(*interpolationShader)(void* outVertexOutput, const void* v1, const void* v2, const void* v3, glm::vec3 barycentrics, const void* uniform);
		glm::vec4(*fragmentShader)(const void* vertexOutput, const void* uniform);
		glm::vec4(*blendShader)(const glm::vec4& src, const glm::vec4& dst, const void* uniform);

		PipelineState state;

		uint32_t vertexStride;
		uint32_t vOutStride;

		Framebuffer framebuffer;

		Texture<glm::vec1>* depthBuffer;

		std::vector<DrawCallData> drawCalls;
	};

	void clear() 
	{
		commands.clear();
	}

private:
	std::vector<DrawCallBatchCommand> commands;

	template<Pipeline P>
	friend class CommandBufferRecording;

	friend class Renderer;
};

template<Pipeline P>
class CommandBufferRecording
{
	using Uniform = P::Uniform;
	using VInput = P::VInput;
	using VOutput = P::VOutput;

public:
	explicit CommandBufferRecording(const PipelineState state) : pipelineState(state) {}
	
	void bindUniform(const Uniform& uniform) { uniformDatas.push_back(uniform); }
	
	void draw(std::span<const VInput> vertexData)
	{
		DrawCall data{
			.vertexData = vertexData.data(),
			.indexData = nullptr,
			.uniformData = &uniformDatas.back(),
			.vertexCount = static_cast<uint32_t>(vertexData.size() / 3 * 3),
		};
		drawCalls.push_back(data);
	}

	void drawIndexed(std::span<const VInput> vertexData, std::span<const uint32_t> indexData)
	{
		DrawCall data{
			.vertexData = vertexData.data(),
			.indexData = indexData.data(),
			.uniformData = &uniformDatas.back(),
			.vertexCount = static_cast<uint32_t>(indexData.size() / 3 * 3),
		};
		drawCalls.push_back(data);
	}

	template<PixelFormat T>
	void commit(CommandBuffer& commandBuffer, Texture<T>& framebuffer, Texture<glm::vec1>* depthBuffer = nullptr) const
	{
		CommandBuffer::DrawCallBatchCommand cmd{
			.vertexShader = [](void* outVertexOutput, const void* vertexInput, const void* uniform)
			{
				const VInput* v_in = static_cast<const VInput*>(vertexInput);
				const Uniform* uni = static_cast<const Uniform*>(uniform);
				VOutput* v_out = static_cast<VOutput*>(outVertexOutput);
				*v_out = P::vertexShader(v_in, uni);
			},
			.interpolationShader = [](void* outVertexOutput, const void* v1, const void* v2, const void* v3, glm::vec3 barycentrics, const void* uniform)
			{
				const VOutput* vertexOutput1 = static_cast<const VOutput*>(v1);
				const VOutput* vertexOutput2 = static_cast<const VOutput*>(v2);
				const VOutput* vertexOutput3 = static_cast<const VOutput*>(v3);
				const Uniform* uni = static_cast<const Uniform*>(uniform);
				VOutput* v_out = static_cast<VOutput*>(outVertexOutput);
				*v_out = P::interpolationShader(vertexOutput1, vertexOutput2, vertexOutput3, barycentrics, uni);
			},
			.fragmentShader = [](const void* vertexOutput, const void* uniform)-> glm::vec4
			{
				const Uniform* uni = static_cast<const Uniform*>(uniform);
				const VOutput* vertexOutputPtr = static_cast<const VOutput*>(vertexOutput);
				return P::fragmentShader(vertexOutputPtr, uni);
			},
			.blendShader = nullptr,
			.state = pipelineState,
			.vertexStride = sizeof(VInput),
			.vOutStride = sizeof(VOutput),
			.framebuffer = {
				.sample = [](void* texture, const glm::uvec2& pos) -> glm::vec4
				{
					Texture<T>* tex = static_cast<Texture<T>*>(texture);
					return tex->getPixel(pos, true);
				},
				.write = [](void* texture, const glm::vec4& value, const glm::uvec2& pos)
				{
					Texture<T>* tex = static_cast<Texture<T>*>(texture);
					tex->setPixel(pos, value);
				},
				.texture = &framebuffer,
			},
			.depthBuffer = depthBuffer,
		};

		if constexpr (HasBlendShader<P>) 
		{
			cmd.blendShader = [](const glm::vec4& src, const glm::vec4& dst, const void* uniform)-> glm::vec4
			{
				const Uniform* uni = static_cast<const Uniform*>(uniform);
				return P::blendShader(src, dst, uni);
			};
		}

		cmd.drawCalls.reserve(this->drawCalls.size());
		for (const DrawCall& dc : this->drawCalls) 
		{
			cmd.drawCalls.emplace_back(
				static_cast<const void*>(dc.uniformData),
				static_cast<const void*>(dc.vertexData),
				dc.indexData,
				dc.vertexCount
			);
		}

		commandBuffer.commands.push_back(std::move(cmd));
	}

	void reserve(uint32_t drawcalls, uint32_t uniforms)
	{
		drawCalls.reserve(drawcalls);
		uniformDatas.reserve(uniforms);
	}

	void clear()
	{
		drawCalls.clear();
		uniformDatas.clear();
	}

private:
	struct DrawCall
	{
		const VInput* vertexData;
		const uint32_t* indexData;
		const Uniform* uniformData;

		uint32_t vertexCount;
	};

	PipelineState pipelineState;

	std::vector<Uniform> uniformDatas{};
	std::vector<DrawCall> drawCalls;

	void* framebuffer = nullptr;
};