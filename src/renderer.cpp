#include "renderer.hpp"
#include "command_buffer.hpp"


Renderer::Renderer()
	: framesize(800, 600), activeExtent(framesize), cpuCount(std::max<uint32_t>(1, std::thread::hardware_concurrency())), phaseBarrier(cpuCount + 1)
{
	currentPhase.store(Phase::Idle);
	prevFrame = std::chrono::steady_clock::now();
	vertexStarts.resize(cpuCount);

	for (uint32_t i = 0; i < cpuCount; ++i) 
	{
		threads.emplace_back([this, i](const std::stop_token& stopToken) 
		{
			threadRun(stopToken, i);
		});
	}
}

Renderer::~Renderer()
{
	currentPhase.store(Phase::Shutdown);
	phaseBarrier.arrive_and_wait();
}

namespace
{
	template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
	template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

	constexpr uint32_t binNodesPerTile = 16;
	constexpr uint32_t binNodesPerTriangle = 4;
	constexpr uint32_t maxBinNodes = 1u << 23;

	void clipLerpRaw(uint8_t* dst, const uint8_t* a, const uint8_t* b, const float t, const uint32_t vOutStride)
	{
		const float* fa = reinterpret_cast<const float*>(a);
		const float* fb = reinterpret_cast<const float*>(b);
		float* fd = reinterpret_cast<float*>(dst);
		const uint32_t floatCount = vOutStride / static_cast<uint32_t>(sizeof(float));
		for (uint32_t i = 0; i < floatCount; ++i)
			fd[i] = fa[i] + (fb[i] - fa[i]) * t;
	}
}

void Renderer::execute(const CommandBuffer& commandBuffer)
{
	vertexTime = 0.0f;
	binningTime = 0.0f;
	fragmentTime = 0.0f;
	computeTime = 0.0f;
	clearTime = 0.0f;
	binningOverflowCounter.store(0, std::memory_order_relaxed);
	clipOverflowCounter.store(0, std::memory_order_relaxed);

	for (const CommandBuffer::Command& command : commandBuffer.commands)
	{
		const auto commandStart = std::chrono::steady_clock::now();

		std::visit(overloaded {
		[&](const CommandBuffer::DrawCallBatchCommand& arg) 
			{
				currentDrawCall = &arg;
				currentPipeline = &commandBuffer.pipelines[arg.pipelineID];

				const glm::uvec2 extent = arg.extent.value_or(framesize);
				activeDownsample = arg.extent ? 1u : downsample;
				if (extent != activeExtent)
				{
					activeExtent = extent;
					initTiles();
				}

				currentClearTargetCount = 0;
				for (const std::optional<CommandBuffer::FillTarget>& colorClear : arg.colorClears)
					if (colorClear)
						currentClearTargets[currentClearTargetCount++] = *colorClear;
				if (arg.depthClear)
					currentClearTargets[currentClearTargetCount++] = *arg.depthClear;
				if (currentClearTargetCount > 0)
					dispatchClear();

				uint32_t vertices = 0;
				uint32_t triangles = 0;
				drawInfos.clear();
				drawInfos.reserve(arg.drawCalls.size());
				for (const auto& drawCall : arg.drawCalls)
				{
					const uint32_t triCount = (drawCall.indexCount > 0 ? drawCall.indexCount : drawCall.vertexCount) / 3;
					if (triCount > 0)
					{
						drawInfos.push_back({
							.vertexBase = vertices, 
							.triBase = triangles, 
							.triCount = triCount,
							.indexData = drawCall.indexData, 
							.uniform = drawCall.uniform,
							.instance = drawCall.instanceData });
					}

					vertices += drawCall.vertexCount;
					triangles += triCount;
				}
				totalTriangles = triangles;
				totalVertices = vertices;
				triangleUniforms.resize(triangles);
				if (currentPipeline->instanced)
					triangleInstances.resize(triangles);
				const uint32_t binNodeDemand = binNodesPerTile * tileCount + binNodesPerTriangle * triangles;
				if (binNodeDemand > binningScratchpad.size())
					binningScratchpad.resize(std::min(binNodeDemand, maxBinNodes));
				initVertexStarts();
				geometryScratchpad.resize(static_cast<size_t>(vertices) * currentPipeline->vOutStride);
				clipcodes.resize(vertices);
				cullGeomScratchpad.resize(std::max(100000u, static_cast<uint32_t>(geometryScratchpad.size() / 9 * 3)));

				triangleCounter.store(0, std::memory_order_relaxed);
				binningCounter.store(0, std::memory_order_relaxed);
				binningCullCounter.store(0, std::memory_order_relaxed);
				tileCounter.store(0, std::memory_order_relaxed);

				currentPhase.store(Phase::Vertex, std::memory_order_release);
				phaseBarrier.arrive_and_wait();
				phaseBarrier.arrive_and_wait();

				const auto vertexEnd = std::chrono::steady_clock::now();

				currentPhase.store(Phase::Binning, std::memory_order_release);
				phaseBarrier.arrive_and_wait();
				phaseBarrier.arrive_and_wait();

				const auto binningEnd = std::chrono::steady_clock::now();

				currentPhase.store(Phase::Fragment, std::memory_order_release);
				phaseBarrier.arrive_and_wait();
				phaseBarrier.arrive_and_wait();

				const auto fragmentEnd = std::chrono::steady_clock::now();

				vertexTime += std::chrono::duration<float, std::milli>(vertexEnd - commandStart).count();
				binningTime += std::chrono::duration<float, std::milli>(binningEnd - vertexEnd).count();
				fragmentTime += std::chrono::duration<float, std::milli>(fragmentEnd - binningEnd).count();

				currentPhase.store(Phase::Idle, std::memory_order_release);
			},
			[&](const CommandBuffer::ClearCommand& arg)
			{
				currentClearTargets[0] = arg.target;
				currentClearTargetCount = 1;

				dispatchClear();
			},
			[&](const CommandBuffer::ComputeCommand& arg)
			{
				currentComputeCall = &arg;

				computeCounter.store(0, std::memory_order_relaxed);

				currentPhase.store(Phase::Compute, std::memory_order_release);
				phaseBarrier.arrive_and_wait();
				phaseBarrier.arrive_and_wait();

				const auto computeEnd = std::chrono::steady_clock::now();

				computeTime += std::chrono::duration<float, std::milli>(computeEnd - commandStart).count();
			}
		}, command);
	}
}

void Renderer::endFrame()
{
	const auto now = std::chrono::steady_clock::now();
	frameTime = std::chrono::duration<float, std::milli>(now - prevFrame).count();
	prevFrame = now;
}

void Renderer::threadRun(const std::stop_token& stopToken, const uint32_t threadID)
{
	while (!stopToken.stop_requested()) 
	{
		phaseBarrier.arrive_and_wait();

		const Phase phase = currentPhase.load();
		if (phase == Phase::Shutdown) 
			break;

		switch (phase) 
		{
		case Phase::Vertex:
			threadRunVertex(threadID);
			break;
		case Phase::Binning:
			threadRunBinning(threadID);
			break;
		case Phase::Fragment:
			threadRunFragment();
			break;
		case Phase::Compute:
			threadRunCompute();
			break;
		case Phase::Clear:
			threadRunClear(threadID);
			break;
		case Phase::Idle:
		case Phase::Shutdown:
			break;
		}

		phaseBarrier.arrive_and_wait();
	}
}

void Renderer::initVertexStarts()
{
	const std::vector<CommandBuffer::DrawCallBatchCommand::DrawCallData>& drawCalls = currentDrawCall->drawCalls;
	const uint32_t vertsPerThread = (totalVertices + cpuCount - 1) / cpuCount;

	uint32_t dcIdx = 0;
	uint32_t dcStartVertices = 0;

	for (uint32_t threadID = 0; threadID < cpuCount; ++threadID)
	{
		const uint32_t vertIdx = threadID * vertsPerThread;

		while (dcIdx < drawCalls.size() && vertIdx >= dcStartVertices + drawCalls[dcIdx].vertexCount)
		{
			dcStartVertices += drawCalls[dcIdx].vertexCount;
			dcIdx++;
		}

		vertexStarts[threadID] = { dcIdx, dcStartVertices };
	}
}

void Renderer::threadRunVertex(const uint32_t threadID) {
	const uint32_t vertsPerThread = (totalVertices + cpuCount - 1) / cpuCount;
	const uint32_t vertIdx = threadID * vertsPerThread;
	const uint32_t vertNum = std::min(vertsPerThread, totalVertices - vertIdx);

	if (vertNum == 0)
		return;

	uint32_t dcIdx = vertexStarts[threadID].drawIndex;
	uint32_t dcStartVertices = vertexStarts[threadID].vertexBase;

	uint32_t currentIdx = vertIdx;
	uint32_t remainingVertices = vertNum;

	const std::vector<CommandBuffer::DrawCallBatchCommand::DrawCallData>& drawCalls = currentDrawCall->drawCalls;
	const size_t vertexStride = currentPipeline->vertexStride;
	const size_t vOutStride = currentPipeline->vOutStride;

	const auto vertexRange = currentPipeline->vertexRange;

	while (remainingVertices > 0 && dcIdx < drawCalls.size())
	{
		const CommandBuffer::DrawCallBatchCommand::DrawCallData& drawCall = drawCalls[dcIdx];

		const uint32_t localIdx = currentIdx - dcStartVertices;
		const uint32_t verticesToProcess = std::min(remainingVertices, drawCall.vertexCount - localIdx);

		uint8_t* dstGeometryBytes = geometryScratchpad.data() + static_cast<uintptr_t>(currentIdx) * vOutStride;

		const uint8_t* srcVertexBytes = static_cast<const uint8_t*>(drawCall.vertexData) + static_cast<uintptr_t>(localIdx) * vertexStride;

		VertexArgs args{
			.vertexInput = srcVertexBytes,
			.instanceInput = drawCall.instanceData,
			.uniform = drawCall.uniform,
			.vertexOutput = dstGeometryBytes,
			.clipcodes = clipcodes.data() + currentIdx,
			.vertexCount = verticesToProcess,
			.framesize = activeExtent
		};

		vertexRange(args);

		currentIdx += verticesToProcess;
		remainingVertices -= verticesToProcess;
		dcStartVertices += drawCall.vertexCount;
		dcIdx++;
	}
}

static bool cullTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const PipelineState::CullMode mode) noexcept
{
	if (mode == PipelineState::CullMode::None) 
	{
		return false;
	}

	const float crossZ = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);

	if (std::abs(crossZ) < 1e-6f) 
	{
		return true;
	}

	if (mode == PipelineState::CullMode::Back) 
	{
		return crossZ > 0.0f;
	}
	if (mode == PipelineState::CullMode::Front) 
	{
		return crossZ < 0.0f;
	}

	return false;
}

const VOutBase* Renderer::resolveVertex(const uint32_t slot, const uint32_t vOutStride) const
{
	if (slot & CULL_BIT)
		return reinterpret_cast<const VOutBase*>(cullGeomScratchpad.data() + static_cast<size_t>(slot & ~CULL_BIT) * vOutStride);
	return reinterpret_cast<const VOutBase*>(geometryScratchpad.data() + static_cast<size_t>(slot) * vOutStride);
}

void Renderer::threadRunBinning(const uint32_t threadID)
{
	const uint32_t vOutStride = currentPipeline->vOutStride;
	const bool instanced = currentPipeline->instanced;
	const PipelineState::CullMode cullMode = currentPipeline->state.cullMode;
	const glm::ivec2 maxTileIndex = glm::ivec2((activeExtent.x + 15u) / 16u, (activeExtent.y + 15u) / 16u) - glm::ivec2(1);

	if (tileCount == 0)
		return;
	uint32_t* const myHeads = tileHeads.data() + static_cast<size_t>(threadID) * tileCount;
	for (uint32_t i = 0; i < tileCount; ++i)
		myHeads[i] = UINT32_MAX;

	constexpr uint32_t nodeBlock = 256;
	uint32_t nodeCursor = 0;
	uint32_t nodeEnd = 0;

	const uint32_t cullCapacity = static_cast<uint32_t>(cullGeomScratchpad.size() / vOutStride);
	const uint32_t binNodeCapacity = static_cast<uint32_t>(binningScratchpad.size());

	const auto makeClipVertex = [&](const VOutBase* a, const VOutBase* b, const float t) -> uint32_t
	{
		const uint32_t idx = binningCullCounter.fetch_add(1, std::memory_order_relaxed);
		if (idx >= cullCapacity) [[unlikely]]
		{
			clipOverflowCounter.fetch_add(1, std::memory_order_relaxed);
			return INVALID_SLOT;
		}

		uint8_t* dst = cullGeomScratchpad.data() + static_cast<size_t>(idx) * vOutStride;
		clipLerpRaw(dst, reinterpret_cast<const uint8_t*>(a), reinterpret_cast<const uint8_t*>(b), t, vOutStride);

		VOutBase* out = reinterpret_cast<VOutBase*>(dst);
		const float oneOverW = 1.0f / out->clipPosition.w;
		out->position = out->clipPosition * oneOverW;
		out->position.x = (out->position.x + 1.0f) * 0.5f * static_cast<float>(activeExtent.x);
		out->position.y = (1.0f - out->position.y) * 0.5f * static_cast<float>(activeExtent.y);
		out->position.w = oneOverW;
		return idx | CULL_BIT;
	};

	const auto emitTriangle = [&](const uint32_t triID, const DrawInfo& info, const uint32_t sa, const uint32_t sb, const uint32_t sc)
	{
		const VOutBase* a = resolveVertex(sa, vOutStride);
		const VOutBase* b = resolveVertex(sb, vOutStride);
		const VOutBase* c = resolveVertex(sc, vOutStride);

		if (!(a->position.w > 0.0f) || !(b->position.w > 0.0f) || !(c->position.w > 0.0f))
			return;

		if (cullTriangle(a->position, b->position, c->position, cullMode))
			return;

		float tpw = 0.0f;

		if (currentPipeline->getUV != nullptr) {
			const auto getUV = currentPipeline->getUV;

			const glm::vec2 uv0 = getUV(a);
			const glm::vec2 uv1 = getUV(b);
			const glm::vec2 uv2 = getUV(c);

			const glm::vec2 uvEdge1 = uv1 - uv0;
			const glm::vec2 uvEdge2 = uv2 - uv0;
			const float uvArea = std::abs(uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x) * 0.5f;

			const glm::vec2 sEdge1 = glm::vec2(b->position) - glm::vec2(a->position);
			const glm::vec2 sEdge2 = glm::vec2(c->position) - glm::vec2(a->position);
			const float screenArea = std::abs(sEdge1.x * sEdge2.y - sEdge1.y * sEdge2.x) * 0.5f;

			if (uvArea > 0.00001f && screenArea > 0.00001f) {
				tpw = std::sqrt(uvArea / screenArea);
			}
		}

		const glm::vec2 pa(a->position), pb(b->position), pc(c->position);
		const glm::vec2 bboxMin = glm::min(glm::min(pa, pb), pc);
		const glm::vec2 bboxMax = glm::max(glm::max(pa, pb), pc);

		const glm::vec2 tileMinF = glm::clamp(glm::floor(bboxMin / 16.0f), glm::vec2(0.0f), glm::vec2(maxTileIndex));
		const glm::vec2 tileMaxF = glm::clamp(glm::floor(bboxMax / 16.0f), glm::vec2(0.0f), glm::vec2(maxTileIndex));
		const glm::ivec2 tileMin = glm::ivec2(tileMinF);
		const glm::ivec2 tileMax = glm::ivec2(tileMaxF);

		triangleUniforms[triID] = info.uniform;
		if (instanced)
			triangleInstances[triID] = info.instance;

		for (int32_t y = tileMin.y; y <= tileMax.y; ++y)
		{
			for (int32_t x = tileMin.x; x <= tileMax.x; ++x)
			{
				if (nodeCursor == nodeEnd)
				{
					nodeCursor = binningCounter.fetch_add(nodeBlock, std::memory_order_relaxed);
					nodeEnd = nodeCursor + nodeBlock;
				}
				const uint32_t nodeIdx = nodeCursor++;
				if (nodeIdx >= binNodeCapacity) [[unlikely]]
				{
					binningOverflowCounter.fetch_add(1, std::memory_order_relaxed);
					return;
				}

				BinNode& node = binningScratchpad[nodeIdx];
				node.triangleID = triID;
				node.v[0] = sa;
				node.v[1] = sb;
				node.v[2] = sc;
				node.tpw = tpw;

				const uint32_t tileIdx = static_cast<uint32_t>(y) * tileRowSize + static_cast<uint32_t>(x);
				node.next = myHeads[tileIdx];
				myHeads[tileIdx] = nodeIdx;
			}
		}
	};

	constexpr uint32_t triangleChunk = 32;

	const DrawInfo* cachedInfo = nullptr;

	while (true)
	{
		const uint32_t chunkStart = triangleCounter.fetch_add(triangleChunk, std::memory_order_relaxed);

		if (chunkStart >= totalTriangles)
			break;

		const uint32_t chunkEnd = std::min(chunkStart + triangleChunk, totalTriangles);

		for (uint32_t nextTri = chunkStart; nextTri < chunkEnd; ++nextTri)
		{
			if (cachedInfo == nullptr || nextTri - cachedInfo->triBase >= cachedInfo->triCount) [[unlikely]]
			{
				uint32_t lo = 0;
				uint32_t hi = static_cast<uint32_t>(drawInfos.size());
				while (lo + 1 < hi)
				{
					const uint32_t mid = (lo + hi) / 2;
					if (drawInfos[mid].triBase <= nextTri)
						lo = mid;
					else
						hi = mid;
				}
				cachedInfo = &drawInfos[lo];
			}
			const DrawInfo& info = *cachedInfo;
			const uint32_t localTri = nextTri - info.triBase;

			uint32_t slot[3];
			if (info.indexData != nullptr)
			{
				slot[0] = info.vertexBase + info.indexData[localTri * 3 + 0];
				slot[1] = info.vertexBase + info.indexData[localTri * 3 + 1];
				slot[2] = info.vertexBase + info.indexData[localTri * 3 + 2];
			}
			else
			{
				slot[0] = info.vertexBase + localTri * 3 + 0;
				slot[1] = info.vertexBase + localTri * 3 + 1;
				slot[2] = info.vertexBase + localTri * 3 + 2;
			}

			const uint8_t c0 = clipcodes[slot[0]];
			const uint8_t c1 = clipcodes[slot[1]];
			const uint8_t c2 = clipcodes[slot[2]];

			if ((c0 & c1 & c2) != 0)
				continue;

			if (((c0 | c1 | c2) & CLIP_NEAR_PLANE) == 0)
			{
				emitTriangle(nextTri, info, slot[0], slot[1], slot[2]);
				continue;
			}

			const VOutBase* tv[3] = {
				resolveVertex(slot[0], vOutStride),
				resolveVertex(slot[1], vOutStride),
				resolveVertex(slot[2], vOutStride),
			};
			const float dist[3] = {
				tv[0]->clipPosition.z + tv[0]->clipPosition.w,
				tv[1]->clipPosition.z + tv[1]->clipPosition.w,
				tv[2]->clipPosition.z + tv[2]->clipPosition.w,
			};

			uint32_t poly[4];
			uint32_t polyCount = 0;
			for (uint32_t i = 0; i < 3; ++i)
			{
				const uint32_t j = (i + 1) % 3;
				const bool insideI = dist[i] >= 0.0f;
				const bool insideJ = dist[j] >= 0.0f;

				if (insideI)
					poly[polyCount++] = slot[i];
				if (insideI != insideJ)
				{
					const float t = dist[i] / (dist[i] - dist[j]);
					const uint32_t clipped = makeClipVertex(tv[i], tv[j], t);
					if (clipped == INVALID_SLOT) [[unlikely]]
					{
						polyCount = 0;
						break;
					}
					poly[polyCount++] = clipped;
				}
			}
			for (uint32_t k = 1; k + 1 < polyCount; ++k)
				emitTriangle(nextTri, info, poly[0], poly[k], poly[k + 1]);
		}
	}
}

void Renderer::threadRunFragment()
{
	std::vector<BinNode> localBinNodes;

	const CommandBuffer::DrawCallBatchCommand& batch = *currentDrawCall;
	const uint32_t vOutStride = currentPipeline->vOutStride;
	const bool instanced = currentPipeline->instanced;

	while (true)
	{
		const uint32_t tileIdx = tileCounter.fetch_add(1);

		if (tileIdx >= tileCount)
			break;

		glm::ivec2 tileCoords = glm::ivec2((tileIdx) % tileRowSize, (tileIdx) / tileRowSize) * 16;
		localBinNodes.clear();

		for (uint32_t t = 0; t < cpuCount; ++t)
		{
			uint32_t nodeIdx = tileHeads[static_cast<size_t>(t) * tileCount + tileIdx];
			while (nodeIdx != UINT32_MAX)
			{
				const BinNode& node = binningScratchpad[nodeIdx];
				localBinNodes.push_back(node);
				nodeIdx = node.next;
			}
		}

		std::ranges::sort(localBinNodes, [](const BinNode& a, const BinNode& b) { return a.triangleID < b.triangleID; });

		const PipelineState& state = currentPipeline->state;
		auto rasterize = currentPipeline->rasterizeTriangle;

		for (const BinNode& node : localBinNodes)
		{
			const VOutBase* v1 = resolveVertex(node.v[0], vOutStride);
			const VOutBase* v2 = resolveVertex(node.v[1], vOutStride);
			const VOutBase* v3 = resolveVertex(node.v[2], vOutStride);

			const glm::vec2 p1 = glm::vec2(v1->position);
			const glm::vec2 p2 = glm::vec2(v2->position);
			const glm::vec2 p3 = glm::vec2(v3->position);

			const glm::vec2 bboxMin = glm::min(glm::min(p1, p2), p3);
			const glm::vec2 bboxMax = glm::max(glm::max(p1, p2), p3);

			const glm::vec2 tileMin = glm::vec2(tileCoords);
			const glm::vec2 tileMax = glm::vec2(glm::min(tileCoords + glm::ivec2(15), glm::ivec2(activeExtent) - glm::ivec2(1)));
			const glm::ivec2 start = glm::ivec2(glm::clamp(bboxMin, tileMin, tileMax));
			const glm::ivec2 end = glm::ivec2(glm::clamp(bboxMax, tileMin, tileMax));

			const float triangleArea = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
			if (std::abs(triangleArea) < 0.0001f)
				continue;

			const RasterArgs args{
				.v1 = v1,
				.v2 = v2,
				.v3 = v3,
				.uniform = triangleUniforms[node.triangleID],
				.instance = instanced ? triangleInstances[node.triangleID] : nullptr,
				.framebuffers = batch.framebuffers.data(),
				.depthBuffer = batch.depthBuffer,
				.start = start,
				.end = end,
				.invArea = 1.0f / triangleArea,
				.downsample = activeDownsample,
				.tpw = node.tpw,
				.state = state,
			};
			rasterize(args);
		}
	}
}

void Renderer::threadRunCompute() {
	while (true)
	{
		const uint32_t nextCompute = computeCounter.fetch_add(1);

		if (currentComputeCall->totalThreads <= nextCompute)
			break;

		const glm::uvec3 numWorkGroups = currentComputeCall->threads;
		const glm::uvec3 localGroupSize = currentComputeCall->localGroupSize;

		const glm::uvec3 totalGlobalThreads = numWorkGroups * localGroupSize;

		glm::uvec3 globalInvocationID{
			nextCompute % totalGlobalThreads.x,
			(nextCompute / totalGlobalThreads.x) % totalGlobalThreads.y,
			nextCompute / (totalGlobalThreads.x * totalGlobalThreads.y)
		};

		ComputeContext ctx{
			.numWorkGroups = numWorkGroups,
			.globalInvocationID = globalInvocationID,
			.localInvocationID = globalInvocationID % localGroupSize,
			.workGroupID = globalInvocationID / localGroupSize
		};

		currentComputeCall->computeShader(ctx, currentComputeCall->uniform);
	}
}

void Renderer::dispatchClear()
{
	const auto clearStart = std::chrono::steady_clock::now();

	currentPhase.store(Phase::Clear, std::memory_order_release);
	phaseBarrier.arrive_and_wait();
	phaseBarrier.arrive_and_wait();

	const auto clearEnd = std::chrono::steady_clock::now();
	clearTime += std::chrono::duration<float, std::milli>(clearEnd - clearStart).count();

	currentPhase.store(Phase::Idle, std::memory_order_release);
}

void Renderer::threadRunClear(const uint32_t threadID)
{
	for (uint32_t i = 0; i < currentClearTargetCount; ++i)
	{
		const CommandBuffer::FillTarget& target = currentClearTargets[i];

		const uint32_t perThread = (target.pixelCount + cpuCount - 1) / cpuCount;
		const uint32_t begin = threadID * perThread;
		const uint32_t end = std::min(begin + perThread, target.pixelCount);

		if (begin < end)
			target.fillRange(target.texture, begin, end, target.valueBytes);
	}
}

void Renderer::initTiles()
{
	tileRowSize = (activeExtent.x + 15) / 16;
	tileCount = tileRowSize * ((activeExtent.y + 15) / 16);
	tileHeads.assign(static_cast<size_t>(tileCount) * cpuCount, UINT32_MAX);
}
