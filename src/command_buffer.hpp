#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <glm.hpp>
#include <limits>
#include <optional>
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
	const void* instanceInput;
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
	const void* instance;

	void* const* framebuffers;
	Texture<glm::vec1>* depthBuffer;

	glm::ivec2 start;
	glm::ivec2 end;
	float invArea;
	uint32_t downsample;
	float tpw;

	PipelineState state;
};

struct ClearState
{
	std::array<std::optional<glm::vec4>, kMaxColorTargets> colors;
	std::optional<float> depth;
};

class CommandBuffer
{
public:
	struct PipelineData
	{
		void(*vertexRange)(const VertexArgs& args);
		void(*rasterizeTriangle)(const RasterArgs& args);
		glm::vec2(*getUV)(const void* vOut);

		PipelineState state;

		uint32_t vertexStride;
		uint32_t vOutStride;
		bool instanced;
	};

	struct FillTarget
	{
		void* texture;
		uint32_t pixelCount;
		void (*fillRange)(void* texture, uint32_t begin, uint32_t end, const void* valueBytes);
		alignas(16) uint8_t valueBytes[16];
	};

	struct ClearCommand
	{
		FillTarget target;
	};

	struct DrawCallBatchCommand
	{
		struct DrawCallData
		{
			const void* uniform;
			const void* vertexData;
			const void* instanceData;
			const uint32_t* indexData;

			uint32_t vertexCount;
			uint32_t indexCount;
		};

		PipelineID pipelineID;

		std::array<void*, kMaxColorTargets> framebuffers{};
		Texture<glm::vec1>* depthBuffer;
		std::optional<glm::uvec2> extent;

		std::array<std::optional<FillTarget>, kMaxColorTargets> colorClears;
		std::optional<FillTarget> depthClear;

		std::vector<DrawCallData> drawCalls;
	};

	struct ComputeCommand
	{
		

		void(*computeShader)(const ComputeContext& ctx, const void* uniform);
		const void* uniform;
		glm::uvec3 threads;
		glm::uvec3 localGroupSize;
		uint32_t totalThreads;
	};

	void clear()
	{
		commands.clear();
	}

	template<PixelFormat T>
	[[nodiscard]] static FillTarget makeFillTarget(Texture<T>& texture, const glm::vec4 value)
	{
		static_assert(sizeof(T) <= 16);

		FillTarget target{
			.texture = &texture,
			.pixelCount = texture.getSize().x * texture.getSize().y,
			.fillRange = [](void* tex, const uint32_t begin, const uint32_t end, const void* valueBytes)
			{
				Texture<T>& t = *static_cast<Texture<T>*>(tex);
				std::ranges::fill(t.data() + begin, t.data() + end, *static_cast<const T*>(valueBytes));
			},
		};

		const T encoded = texture.encodeClearValue(value);
		std::memcpy(target.valueBytes, &encoded, sizeof(T));
		return target;
	}

	template<PixelFormat T>
	void clearTexture(Texture<T>& texture, const glm::vec4 value)
	{
		commands.emplace_back(ClearCommand{ .target = makeFillTarget(texture, value) });
	}

	template<HasFragmentShader P>
	PipelineID registerPipeline(PipelineState state);

	template<DepthOnlyPipeline P>
	PipelineID registerPipeline(PipelineState state);

	template<ComputePipeline P>
	void commitCompute(const P::Uniform& uniform, glm::uvec3 localGroupSize, glm::vec3 groupCount);

private:
	using Command = std::variant<
		DrawCallBatchCommand,
		ComputeCommand,
		ClearCommand
	>;

	std::vector<Command> commands;
	std::vector<PipelineData> pipelines;

	template<Pipeline P>
	friend class CommandBufferRecording;

	friend class Renderer;
};

template<PixelFormat... T>
class Attachments
{
public:
	explicit Attachments(Texture<T>&... textures) : textures(textures...)
	{
		const glm::uvec2 size = std::get<0>(this->textures).getSize();
		assert(((textures.getSize() == size) && ...) && "All attachments must have the same size.");
	}

	void bind(CommandBuffer::DrawCallBatchCommand& cmd, const ClearState& clearState) const
	{
		[&]<size_t... I>(std::index_sequence<I...>)
		{
			((cmd.framebuffers[I] = &std::get<I>(textures), clearState.colors[I] ? void(cmd.colorClears[I] = CommandBuffer::makeFillTarget(std::get<I>(textures), *clearState.colors[I])) : void()), ...);
		}(std::index_sequence_for<T...>{});
	}

private:
	std::tuple<Texture<T>&...> textures;
};

template<PixelFormat... T>
Attachments(Texture<T>&...) -> Attachments<T...>;

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
typename P::VOutput runVertexShader(const typename P::VInput* vertex, [[maybe_unused]] const void* instance, const typename P::Uniform* uniform)
{
	if constexpr (InstancedPipeline<P>)
		return P::vertexShader(vertex, static_cast<const typename P::InstanceInput*>(instance), uniform);
	else
		return P::vertexShader(vertex, uniform);
}

template<HasFragmentShader P>
FragmentReturn<P> runFragmentShader(const typename P::VOutput* vOut, [[maybe_unused]] const void* instance, const typename P::Uniform* uniform, const float tpw)
{
	if constexpr (InstancedPipeline<P>)
		return P::fragmentShader(vOut, static_cast<const typename P::InstanceInput*>(instance), uniform, tpw);
	else
		return P::fragmentShader(vOut, uniform, tpw);
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
		vOut[i] = runVertexShader<P>(&vIn[i], args.instanceInput, uniform);
		vOut[i].clipPosition = vOut[i].position;

		vertexClipCalc(args, vOut[i], i);

		const float oneOverW = 1.0f / vOut[i].position.w;
		vOut[i].position *= oneOverW;
		vOut[i].position.x = (vOut[i].position.x + 1.0f) * 0.5f * static_cast<float>(args.framesize.x);
		vOut[i].position.y = (1.0f - vOut[i].position.y) * 0.5f * static_cast<float>(args.framesize.y);
		vOut[i].position.w = oneOverW;
	}
}

inline float fillRuleBias(const float stepX, const float stepY)
{
	const bool topLeft = stepX > 0.0f || (stepX == 0.0f && stepY > 0.0f);
	return topLeft ? 0.0f : std::numeric_limits<float>::denorm_min();
}

template<PixelFormat T>
void writeTarget(void* target, const glm::vec4 color, const uint32_t bx0, const uint32_t by0, const uint32_t ds)
{
	Texture<T>& framebuffer = *static_cast<Texture<T>*>(target);
	const glm::uvec2 outputSize = framebuffer.getSize();

	constexpr bool quantize = !std::is_floating_point_v<typename T::value_type>;

	const uint32_t bx1 = glm::min(bx0 + ds, outputSize.x);
	const uint32_t by1 = glm::min(by0 + ds, outputSize.y);
	for (uint32_t by = by0; by < by1; ++by)
		for (uint32_t bx = bx0; bx < bx1; ++bx)
			framebuffer.setPixel(glm::uvec2(bx, by), color, quantize);
}

template<PixelFormat... T>
void writeTargets(const RasterArgs& a, const glm::vec4* colors, const uint32_t bx0, const uint32_t by0, TargetList<T...>)
{
	[&]<size_t... I>(std::index_sequence<I...>)
	{
		(writeTarget<T>(a.framebuffers[I], colors[I], bx0, by0, a.downsample), ...);
	}(std::index_sequence_for<T...>{});
}

template<Pipeline P>
void rasterizeTriangleImpl(const RasterArgs& a)
{
	using VOutput = P::VOutput;
	using Uniform = P::Uniform;

	const VOutput* v1 = static_cast<const VOutput*>(a.v1);
	const VOutput* v2 = static_cast<const VOutput*>(a.v2);
	const VOutput* v3 = static_cast<const VOutput*>(a.v3);
	const Uniform* uni = static_cast<const Uniform*>(a.uniform);

	Texture<glm::vec1>* const depthBuffer = a.depthBuffer;

	const glm::vec2 p1 = glm::vec2(v1->position);
	const glm::vec2 p2 = glm::vec2(v2->position);
	const glm::vec2 p3 = glm::vec2(v3->position);

	const float orientation = std::copysign(1.0f, a.invArea);
	const float weightScale = std::abs(a.invArea);

	const glm::vec3 edgeStepX = orientation * glm::vec3(p2.y - p3.y, p3.y - p1.y, p1.y - p2.y);
	const glm::vec3 edgeStepY = orientation * glm::vec3(p3.x - p2.x, p1.x - p3.x, p2.x - p1.x);
	const glm::vec3 edgeBias(fillRuleBias(edgeStepX.x, edgeStepY.x), fillRuleBias(edgeStepX.y, edgeStepY.y), fillRuleBias(edgeStepX.z, edgeStepY.z));

	const glm::vec3 edgeOriginX(p2.x, p3.x, p1.x);
	const glm::vec3 edgeOriginY(p2.y, p3.y, p1.y);

	for (int32_t y = a.start.y; y <= a.end.y; ++y)
	{
		for (int32_t x = a.start.x; x <= a.end.x; ++x)
		{
			const glm::vec2 pixelCenter = glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
			const glm::vec3 edge = edgeStepX * (pixelCenter.x - edgeOriginX) + edgeStepY * (pixelCenter.y - edgeOriginY);

			if (edge.x < edgeBias.x || edge.y < edgeBias.y || edge.z < edgeBias.z)
				continue;

			const float w0 = edge.x * weightScale;
			const float w1 = edge.y * weightScale;
			const float w2 = 1.0f - w0 - w1;

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
					continue;
				}
			}

			if constexpr (HasFragmentShader<P>)
			{
				using Targets = P::Targets;

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
				FragmentOutputOf<P> fragmentOutput{};
				if constexpr (HasDiscardingFragmentShader<P>)
				{
					const std::optional<FragmentOutputOf<P>> shaded = runFragmentShader<P>(&interpolated, a.instance, uni, a.tpw);
					if (!shaded)
						continue;
					fragmentOutput = *shaded;
				}
				else
				{
					fragmentOutput = runFragmentShader<P>(&interpolated, a.instance, uni, a.tpw);
				}

				glm::vec4 colors[Targets::count];
				std::memcpy(colors, &fragmentOutput, sizeof(colors));

				const uint32_t bx0 = static_cast<uint32_t>(x) * a.downsample;
				const uint32_t by0 = static_cast<uint32_t>(y) * a.downsample;

				if constexpr (HasBlendShader<P>)
				{
					Texture<typename Targets::template At<0>>& first = *static_cast<Texture<typename Targets::template At<0>>*>(a.framebuffers[0]);
					colors[0] = P::blendShader(colors[0], first.getPixel(glm::uvec2(bx0, by0), true), uni);
				}

				writeTargets(a, colors, bx0, by0, Targets{});
			}

			if (a.state.depthWrite)
				depthBuffer->at(glm::uvec2(x, y)).x = depth;
		}
	}
}

template <HasFragmentShader P>
PipelineID CommandBuffer::registerPipeline(const PipelineState state)
{
	pipelines.emplace_back(
		&vertexRangeImpl<P>,
		&rasterizeTriangleImpl<P>,
		nullptr,
		state,
		sizeof(typename P::VInput),
		sizeof(typename P::VOutput),
		InstancedPipeline<P>
	);

	if constexpr (HasAccurateMip<P>)
	{
		pipelines.back().getUV = [](const void* vOut)-> glm::vec2
		{
			const typename P::VOutput* v = static_cast<const P::VOutput*>(vOut);
			return P::getUV(v);
		};
	}

	return static_cast<PipelineID>(pipelines.size() - 1ull);
}

template <DepthOnlyPipeline P>
PipelineID CommandBuffer::registerPipeline(const PipelineState state)
{
	pipelines.emplace_back(
		&vertexRangeImpl<P>,
		&rasterizeTriangleImpl<P>,
		nullptr,
		state,
		sizeof(typename P::VInput),
		sizeof(typename P::VOutput),
		InstancedPipeline<P>
	);

	return static_cast<PipelineID>(pipelines.size() - 1ull);
}

template <ComputePipeline P>
void CommandBuffer::commitCompute(const typename P::Uniform& uniform, glm::uvec3 localGroupSize, glm::vec3 groupCount)
{
	const glm::uvec3 workGroups = glm::uvec3(groupCount);
	const uint32_t totalThreads = workGroups.x * workGroups.y * workGroups.z
		* localGroupSize.x * localGroupSize.y * localGroupSize.z;

	ComputeCommand cmd{
		.computeShader = [](const ComputeContext& ctx, const void* uni)
		{
			const typename P::Uniform* u = static_cast<const typename P::Uniform*>(uni);
			P::computeShader(ctx, u);
		},
		.uniform = &uniform,
		.threads = workGroups,
		.localGroupSize = localGroupSize,
		.totalThreads = totalThreads,
	};

	commands.emplace_back(cmd);
}

template<Pipeline P>
class CommandBufferRecording
{
	using Uniform = P::Uniform;
	using VInput = P::VInput;
	using VOutput = P::VOutput;
	using InstanceInput = InstanceInputOf<P>::type;

public:
	explicit CommandBufferRecording(const PipelineID pipelineID) : pipelineID(pipelineID) {}

	void bindUniform(const Uniform& uniform) { uniformDatas.push_back(uniform); }

	void draw(std::span<const VInput> vertexData) requires (!InstancedPipeline<P>)
	{
		drawCalls.push_back(makeDrawCall(vertexData, nullptr, 1));
	}

	void drawIndexed(std::span<const VInput> vertexData, std::span<const uint32_t> indexData) requires (!InstancedPipeline<P>)
	{
		drawCalls.push_back(makeIndexedDrawCall(vertexData, indexData, nullptr, 1));
	}

	void drawInstanced(std::span<const VInput> vertexData, std::span<const InstanceInput> instanceData) requires InstancedPipeline<P>
	{
		drawCalls.push_back(makeDrawCall(vertexData, instanceData.data(), static_cast<uint32_t>(instanceData.size())));
	}

	void drawIndexedInstanced(std::span<const VInput> vertexData, std::span<const uint32_t> indexData, std::span<const InstanceInput> instanceData) requires InstancedPipeline<P>
	{
		drawCalls.push_back(makeIndexedDrawCall(vertexData, indexData, instanceData.data(), static_cast<uint32_t>(instanceData.size())));
	}

	template<PixelFormat... T> requires HasFragmentShader<P> && std::same_as<TargetList<T...>, typename P::Targets>
	void commit(CommandBuffer& commandBuffer, const Attachments<T...>& attachments, Texture<glm::vec1>* depthBuffer = nullptr, const ClearState& clearState = {}, const std::optional<glm::uvec2> extent = std::nullopt) const
	{
		CommandBuffer::DrawCallBatchCommand cmd = makeBatch(depthBuffer, clearState, extent);
		attachments.bind(cmd, clearState);

		commandBuffer.commands.emplace_back(std::move(cmd));
	}

	template<PixelFormat T> requires HasFragmentShader<P> && std::same_as<TargetList<T>, typename P::Targets>
	void commit(CommandBuffer& commandBuffer, Texture<T>& framebuffer, Texture<glm::vec1>* depthBuffer = nullptr, const ClearState& clearState = {}, const std::optional<glm::uvec2> extent = std::nullopt) const
	{
		commit(commandBuffer, Attachments<T>{ framebuffer }, depthBuffer, clearState, extent);
	}

	void commit(CommandBuffer& commandBuffer, Texture<glm::vec1>& depthBuffer, const ClearState& clearState = {}, const std::optional<glm::uvec2> extent = std::nullopt) const
		requires DepthOnlyPipeline<P>
	{
		commandBuffer.commands.emplace_back(makeBatch(&depthBuffer, clearState, extent));
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
	[[nodiscard]] CommandBuffer::DrawCallBatchCommand makeBatch(Texture<glm::vec1>* depthBuffer, const ClearState& clearState, const std::optional<glm::uvec2> extent) const
	{
		CommandBuffer::DrawCallBatchCommand cmd{
			.pipelineID = pipelineID,
			.depthBuffer = depthBuffer,
			.extent = extent,
			.drawCalls = {},
		};

		if (clearState.depth && depthBuffer != nullptr)
			cmd.depthClear = CommandBuffer::makeFillTarget(*depthBuffer, glm::vec4(*clearState.depth));

		size_t expandedCount = 0;
		for (const DrawCall& dc : this->drawCalls)
			expandedCount += dc.instanceCount;

		cmd.drawCalls.reserve(expandedCount);
		for (const DrawCall& dc : this->drawCalls)
		{
			for (uint32_t instance = 0; instance < dc.instanceCount; ++instance)
			{
				cmd.drawCalls.emplace_back(
					static_cast<const void*>(&uniformDatas[dc.uniformIndex]),
					static_cast<const void*>(dc.vertexData),
					dc.instanceData != nullptr ? static_cast<const void*>(dc.instanceData + instance) : nullptr,
					dc.indexData,
					dc.vertexCount,
					dc.indexCount
				);
			}
		}

		return cmd;
	}

	struct DrawCall
	{
		const VInput* vertexData;
		const InstanceInput* instanceData;
		const uint32_t* indexData;
		uint32_t uniformIndex;

		uint32_t vertexCount;
		uint32_t indexCount;
		uint32_t instanceCount;
	};

	[[nodiscard]] DrawCall makeDrawCall(std::span<const VInput> vertexData, const InstanceInput* instanceData, const uint32_t instanceCount) const
	{
		return DrawCall{
			.vertexData = vertexData.data(),
			.instanceData = instanceData,
			.indexData = nullptr,
			.uniformIndex = static_cast<uint32_t>(uniformDatas.size() - 1),
			.vertexCount = static_cast<uint32_t>(vertexData.size() / 3 * 3),
			.indexCount = 0,
			.instanceCount = instanceCount,
		};
	}

	[[nodiscard]] DrawCall makeIndexedDrawCall(std::span<const VInput> vertexData, std::span<const uint32_t> indexData, const InstanceInput* instanceData, const uint32_t instanceCount) const
	{
		DrawCall data = makeDrawCall(vertexData, instanceData, instanceCount);
		data.indexData = indexData.data();
		data.vertexCount = static_cast<uint32_t>(vertexData.size());
		data.indexCount = static_cast<uint32_t>(indexData.size() / 3 * 3);
		return data;
	}
	PipelineID pipelineID;

	std::vector<Uniform> uniformDatas{};
	std::vector<DrawCall> drawCalls;
};
