#pragma once
#include <cstdint>
#include <glm.hpp>
#include <span>
#include <variant>

#include "pipeline.hpp"
#include "texture.hpp"

template<Pipeline P>
class CommandBufferRecording;

using PipelineID = uint32_t;

enum ClipFlags : uint8_t {
	CLIP_INSIDE = 0,
	CLIP_NEAR_PLANE = 1 << 0,
	CLIP_FAR_PLANE = 1 << 1,
	CLIP_LEFT_PLANE = 1 << 2,
	CLIP_RIGHT_PLANE = 1 << 3,
	CLIP_TOP_PLANE = 1 << 4,
	CLIP_BOTTOM_PLANE = 1 << 5,
};

struct VertexArgs
{
	const void* vertexInput;
	const void* uniform;
	void* vertexOutput;
	uint8_t* clipcodes;
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
	uint32_t downsample;

	PipelineState state;
};

class CommandBuffer
{
public:
	struct PipelineData
	{
		void(*vertexRange)(const VertexArgs& args);
		void(*rasterizeTriangle)(const RasterArgs& args);

		PipelineState state;

		uint32_t vertexStride;
		uint32_t vOutStride;
	};

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

		PipelineData* pipelineData;

		void* framebuffer;
		Texture<glm::vec1>* depthBuffer;

		std::vector<DrawCallData> drawCalls;
	};

	struct ComputeCommand
	{
		

		void(*computeShader)(const ComputeContext& ctx, const void* uniform);
		void* uniform;
		glm::uvec3 threads;
		glm::uvec3 localGroupSize;
		uint32_t totalThreads;
	};

	void clear()
	{
		commands.clear();
	}

	template<Pipeline P, PixelFormat T>
	PipelineID registerPipeline(PipelineState state);

	template<ComputePipeline P>
	void commitCompute(const P::Uniform& uniform, glm::uvec3 localGroupSize, glm::vec3 groupCount);

private:
	using Command = std::variant<
		DrawCallBatchCommand,
		ComputeCommand
	>;

	std::vector<Command> commands;
	std::vector<PipelineData> pipelines;

	template<Pipeline P>
	friend class CommandBufferRecording;

	friend class Renderer;
};

inline void vertexClipCalc(const VertexArgs& args, const VOutBase& out, const uint32_t idx)
{
	const glm::vec4 p = out.clipPosition;

	uint8_t code = CLIP_INSIDE;
	if (p.z + p.w < 0.0f || p.w <= 0.0f) code |= CLIP_NEAR_PLANE;
	if (p.z - p.w > 0.0f)                code |= CLIP_FAR_PLANE;
	if (p.x + p.w < 0.0f)                code |= CLIP_LEFT_PLANE;
	if (p.x - p.w > 0.0f)                code |= CLIP_RIGHT_PLANE;
	if (p.y + p.w < 0.0f)                code |= CLIP_BOTTOM_PLANE;
	if (p.y - p.w > 0.0f)                code |= CLIP_TOP_PLANE;

	args.clipcodes[idx] = code;
}

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
		vOut[i].clipPosition = vOut[i].position;

		vertexClipCalc(args, vOut[i], i);

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

	const uint32_t ds = a.downsample;
	const glm::uvec2 outputSize = framebuffer.getSize();

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
			if (depth > 1.0f)
				continue;
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

			VOutput interpolated{};
			{
				const float invW1 = v1->position.w;
				const float invW2 = v2->position.w;
				const float invW3 = v3->position.w;
				const float denom = invW1 * w0 + invW2 * w1 + invW3 * w2;
				const float invDenom = denom != 0.0f ? 1.0f / denom : 0.0f;
				const float pc0 = invW1 * w0 * invDenom;
				const float pc1 = invW2 * w1 * invDenom;
				const float pc2 = invW3 * w2 * invDenom;

				interpolated.position = w0 * v1->position + w1 * v2->position + w2 * v3->position;

				const float* fa = reinterpret_cast<const float*>(v1);
				const float* fb = reinterpret_cast<const float*>(v2);
				const float* fc = reinterpret_cast<const float*>(v3);
				float* fd = reinterpret_cast<float*>(&interpolated);
				constexpr uint32_t skip = sizeof(VOutBase) / sizeof(float);
				constexpr uint32_t count = sizeof(VOutput) / sizeof(float);
				for (uint32_t i = skip; i < count; ++i)
					fd[i] = fa[i] * pc0 + fb[i] * pc1 + fc[i] * pc2;
			}
			glm::vec4 fragmentOutput = P::fragmentShader(&interpolated, uni);

			const uint32_t bx0 = static_cast<uint32_t>(x) * ds;
			const uint32_t by0 = static_cast<uint32_t>(y) * ds;

			if constexpr (HasBlendShader<P>)
			{
				const glm::vec4 background = framebuffer.getPixel(glm::uvec2(bx0, by0), true);
				fragmentOutput = P::blendShader(fragmentOutput, background, uni);
			}

			const uint32_t bx1 = glm::min(bx0 + ds, outputSize.x);
			const uint32_t by1 = glm::min(by0 + ds, outputSize.y);
			for (uint32_t by = by0; by < by1; ++by)
				for (uint32_t bx = bx0; bx < bx1; ++bx)
					framebuffer.setPixel(glm::uvec2(bx, by), fragmentOutput);

			if (a.state.depthWrite)
				depthBuffer->at(glm::uvec2(x, y)).x = depth;
		}
	}
}

template <Pipeline P, PixelFormat T>
PipelineID CommandBuffer::registerPipeline(const PipelineState state)
{
	pipelines.emplace_back(
		&vertexRangeImpl<P>,
		&rasterizeTriangleImpl<P, T>,
		state,
		sizeof(typename P::VInput),
		sizeof(typename P::VOutput)
	);
	return static_cast<PipelineID>(pipelines.size() - 1ull);
}

template <ComputePipeline P>
void CommandBuffer::commitCompute(const typename P::Uniform& uniform, glm::uvec3 localGroupSize, glm::vec3 groupCount)
{
	ComputeCommand cmd{
		.computeShader = [](const ComputeContext& ctx, const void* uni)
		{
			const typename P::Uniform* u = static_cast<P::Uniform*>(uni);
			P::computeShader(ctx, u);
		},
		.uniform = &uniform,
		.threads = groupCount,
		.localGroupSize = localGroupSize,
		.totalThreads = groupCount.x * groupCount.y * groupCount.z,
	};

	commands.emplace_back(cmd);
}

template<Pipeline P>
class CommandBufferRecording
{
	using Uniform = P::Uniform;
	using VInput = P::VInput;
	using VOutput = P::VOutput;

public:
	explicit CommandBufferRecording(const PipelineID pipelineID) : pipelineID(pipelineID) {}

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
			.pipelineData = &commandBuffer.pipelines[pipelineID],
			.framebuffer = &framebuffer,
			.depthBuffer = depthBuffer,
			.drawCalls = {},
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

		commandBuffer.commands.emplace_back(cmd);
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
	PipelineID pipelineID;

	std::vector<Uniform> uniformDatas{};
	std::vector<DrawCall> drawCalls;
};
