#pragma once
#include <cstdint>
#include <glm.hpp>
#include <span>

#include "pipeline.hpp"

class CommandBuffer 
{
public:
	struct DrawCallBatchCommand 
	{
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
		glm::vec4(*blendShader)(const glm::vec4* src, const glm::vec4* dst);
		PipelineState state;

		uint32_t vertexStride;
		uint32_t vOutStride;

		std::vector<DrawCallData> drawCalls;
	};

	void clear() 
	{
		commands.clear();
	}

private:
	std::vector<DrawCallBatchCommand> commands;

	template<typename P, typename Uniform, typename VInput, typename VOutput>
		requires Pipeline<P, Uniform, VInput, VOutput>
	friend class CommandBufferRecording;

	friend class Renderer;
};

template<typename P, typename Uniform, typename VInput, typename VOutput>
	requires Pipeline<P, Uniform, VInput, VOutput>
class CommandBufferRecording
{
public:
	explicit CommandBufferRecording(const PipelineState state) : pipelineState(state) {}
	
	void bindUniform(const Uniform& uniform) { uniformDatas.push_back(uniform); }
	
	void draw(std::span<const VInput> vertexData)
	{
		DrawCall data{
			.vertexData = vertexData.data(),
			.indexData = nullptr,
			.vertexCount = static_cast<uint32_t>(vertexData.size() / 3 * 3),
			.uniformData = &uniformDatas.back()
		};
		drawCalls.push_back(data);
	}

	void drawIndexed(std::span<const VInput> vertexData, std::span<const uint32_t> indexData)
	{
		DrawCall data{
			.vertexData = vertexData.data(),
			.indexData = indexData.data(),
			.vertexCount = static_cast<uint32_t>(indexData.size() / 3 * 3),
			.uniformData = &uniformDatas.back()
		};
		drawCalls.push_back(data);
	}

	void commit(CommandBuffer& commandBuffer) const
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
			.fragmentShader = [](const void* vertexOutput, const void* uniform) -> glm::vec4
			{
				const VOutput* v_out = static_cast<const VOutput*>(vertexOutput);
				const Uniform* uni = static_cast<const Uniform*>(uniform);
				return P::fragmentShader(v_out, uni);
			},
			.blendShader = nullptr,
			.state = pipelineState,
			.vertexStride = sizeof(VInput),
			.vOutStride = sizeof(VOutput),
		};

		if constexpr (HasBlendShader<P>) 
		{
			cmd.blendShader = [](const glm::vec4* src, const glm::vec4* dst) -> glm::vec4
			{
				return P::blendShader(src, dst);
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
		uint32_t vertexCount;

		const Uniform* uniformData;
	};

	PipelineState pipelineState;

	std::vector<Uniform> uniformDatas{};
	std::vector<DrawCall> drawCalls;
};