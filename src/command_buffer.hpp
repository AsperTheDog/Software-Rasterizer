#pragma once
#include <cstdint>
#include <glm.hpp>
#include <span>

#include "pipeline.hpp"
#include "texture.hpp"

template<Pipeline P>
class CommandBufferRecording;

struct VertexArgs
{
	const void* vertexInput;
	const void* uniform;
	void* vertexOutput;
	uint32_t vertexCount;
	glm::uvec2 framesize;
};

struct RasterArgs
{
	const void* v1;
	const void* v2;
	const void* v3;
	const void* uniform;

	void* framebuffer;
	Texture<glm::vec1>* depthBuffer;

	glm::ivec2 start;
	glm::ivec2 end;
	float invArea;

	PipelineState state;
};

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
			uint32_t indexCount;
		};

		void(*vertexRange)(const VertexArgs& args);
		void(*rasterizeTriangle)(const RasterArgs& args);

		PipelineState state;

		uint32_t vertexStride;
		uint32_t vOutStride;

		void* framebuffer;
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
void vertexRangeImpl(const VertexArgs& args)
{
	using VInput = P::VInput;
	using VOutput = P::VOutput;
	using Uniform = P::Uniform;

	const VInput* vIn = static_cast<const VInput*>(args.vertexInput);
	const Uniform* uniform = static_cast<const Uniform*>(args.uniform);
	VOutput* vOut = static_cast<VOutput*>(args.vertexOutput);

	for (uint32_t i = 0; i < args.vertexCount; ++i)
	{
		vOut[i] = P::vertexShader(&vIn[i], uniform);

		const float oneOverW = 1.0f / vOut[i].position.w;
		vOut[i].position *= oneOverW;
		vOut[i].position.x = (vOut[i].position.x + 1.0f) * 0.5f * static_cast<float>(args.framesize.x);
		vOut[i].position.y = (vOut[i].position.y + 1.0f) * 0.5f * static_cast<float>(args.framesize.y);
		vOut[i].position.w = oneOverW;
	}
}

template<Pipeline P, PixelFormat T>
void rasterizeTriangleImpl(const RasterArgs& a)
{
	using VOutput = P::VOutput;
	using Uniform = P::Uniform;

	const VOutput* v1 = static_cast<const VOutput*>(a.v1);
	const VOutput* v2 = static_cast<const VOutput*>(a.v2);
	const VOutput* v3 = static_cast<const VOutput*>(a.v3);
	const Uniform* uni = static_cast<const Uniform*>(a.uniform);

	Texture<T>& framebuffer = *static_cast<Texture<T>*>(a.framebuffer);
	Texture<glm::vec1>* const depthBuffer = a.depthBuffer;

	const glm::vec2 p1 = glm::vec2(v1->position);
	const glm::vec2 p2 = glm::vec2(v2->position);
	const glm::vec2 p3 = glm::vec2(v3->position);

	for (int32_t y = a.start.y; y <= a.end.y; ++y)
	{
		for (int32_t x = a.start.x; x <= a.end.x; ++x)
		{
			const glm::vec2 pixelCenter = glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
			const float w0 = ((p3.x - p2.x) * (pixelCenter.y - p2.y) - (p3.y - p2.y) * (pixelCenter.x - p2.x)) * a.invArea;
			const float w1 = ((p1.x - p3.x) * (pixelCenter.y - p3.y) - (p1.y - p3.y) * (pixelCenter.x - p3.x)) * a.invArea;
			const float w2 = 1.0f - w0 - w1;

			if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
				continue;

			const float depth = w0 * v1->position.z + w1 * v2->position.z + w2 * v3->position.z;
			if (a.state.depthTest)
			{
				const float oldDepth = depthBuffer->at(glm::uvec2(x, y)).x;
				switch (a.state.depthOp)
				{
				case PipelineState::DepthOp::Less:
					if (depth >= oldDepth) 
						continue; 
					break;
				case PipelineState::DepthOp::Equal:
					if (depth != oldDepth) 
						continue; 
					break;
				case PipelineState::DepthOp::Greater:
					if (depth <= oldDepth) 
						continue; 
					break;
				case PipelineState::DepthOp::NotEqual: 
					if (depth == oldDepth) 
						continue; 
					break;
				case PipelineState::DepthOp::Never:
					break;
				}
			}

			VOutput interpolated = P::interpolationShader(v1, v2, v3, glm::vec3(w0, w1, w2), uni);
			glm::vec4 fragmentOutput = P::fragmentShader(&interpolated, uni);

			if constexpr (HasBlendShader<P>)
			{
				const glm::vec4 background = framebuffer.getPixel(glm::uvec2(x, y), true);
				fragmentOutput = P::blendShader(fragmentOutput, background, uni);
			}

			framebuffer.setPixel(glm::uvec2(x, y), fragmentOutput);

			if (a.state.depthWrite)
				depthBuffer->at(glm::uvec2(x, y)).x = depth;
		}
	}
}

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
			.indexCount = 0,
		};
		drawCalls.push_back(data);
	}

	void drawIndexed(std::span<const VInput> vertexData, std::span<const uint32_t> indexData)
	{
		DrawCall data{
			.vertexData = vertexData.data(),
			.indexData = indexData.data(),
			.uniformData = &uniformDatas.back(),
			.vertexCount = static_cast<uint32_t>(vertexData.size()),
			.indexCount = static_cast<uint32_t>(indexData.size() / 3 * 3),
		};
		drawCalls.push_back(data);
	}

	template<PixelFormat T>
	void commit(CommandBuffer& commandBuffer, Texture<T>& framebuffer, Texture<glm::vec1>* depthBuffer = nullptr) const
	{
		CommandBuffer::DrawCallBatchCommand cmd{
			.vertexRange = &vertexRangeImpl<P>,
			.rasterizeTriangle = &rasterizeTriangleImpl<P, T>,
			.state = pipelineState,
			.vertexStride = sizeof(VInput),
			.vOutStride = sizeof(VOutput),
			.framebuffer = &framebuffer,
			.depthBuffer = depthBuffer,
		};

		cmd.drawCalls.reserve(this->drawCalls.size());
		for (const DrawCall& dc : this->drawCalls)
		{
			cmd.drawCalls.emplace_back(
				static_cast<const void*>(dc.uniformData),
				static_cast<const void*>(dc.vertexData),
				dc.indexData,
				dc.vertexCount,
				dc.indexCount
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
		uint32_t indexCount;
	};

	PipelineState pipelineState;

	std::vector<Uniform> uniformDatas{};
	std::vector<DrawCall> drawCalls;
};
