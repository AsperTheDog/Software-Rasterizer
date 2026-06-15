#include "renderer.hpp"
#include "command_buffer.hpp"


Renderer::Renderer()
	: framesize(800, 600),
	binningScratchpad(1 << 21),
	cpuCount(std::max<uint32_t>(1, std::thread::hardware_concurrency())),
	phaseBarrier(cpuCount + 1)
{
	currentPhase.store(Phase::Idle);

	for (uint32_t i = 0; i < cpuCount; ++i) {
		threads.emplace_back([this, i](const std::stop_token& stopToken) {
			threadRun(stopToken, i);
		});
	}
}

Renderer::~Renderer()
{
	currentPhase.store(Phase::Shutdown);
}

void Renderer::execute(const CommandBuffer& commandBuffer)
{
	assert(framebuffer != nullptr && "Framebuffer is not initialized");

	const auto start = std::chrono::high_resolution_clock::now();
	vertexTime = 0.0f;
	binningTime = 0.0f;
	fragmentTime = 0.0f;

	for (const CommandBuffer::DrawCallBatchCommand& command : commandBuffer.commands)
	{
		currentCommandBatch = &command;

		localVOutData.resize(static_cast<size_t>(cpuCount) * command.vOutStride);
		uint32_t vertices = 0;
		for (const auto& drawCall : currentCommandBatch->drawCalls)
		{
			vertices += drawCall.vertexCount;
		}
		geometryScratchpad.resize(static_cast<size_t>(vertices) * command.vOutStride);

		triangleCounter.store(0, std::memory_order_relaxed);
		binningCounter.store(0, std::memory_order_relaxed);
		tileCounter.store(0, std::memory_order_relaxed);

		for (Tile& tile : tiles)
		{
			tile.head.store(UINT32_MAX, std::memory_order_relaxed);
			tile.count.store(0, std::memory_order_relaxed);
		}

		// 2. Execute VERTEX Phase
		currentPhase.store(Phase::Vertex, std::memory_order_release);
		phaseBarrier.arrive_and_wait();
		phaseBarrier.arrive_and_wait();

		auto vertexEnd = std::chrono::high_resolution_clock::now();

		// 3. Execute BINNING Phase
		currentPhase.store(Phase::Binning, std::memory_order_release);
		phaseBarrier.arrive_and_wait();
		phaseBarrier.arrive_and_wait();

		auto binningEnd = std::chrono::high_resolution_clock::now();

		// 4. Execute FRAGMENT Phase
		currentPhase.store(Phase::Fragment, std::memory_order_release);
		phaseBarrier.arrive_and_wait();
		phaseBarrier.arrive_and_wait();

		std::chrono::time_point<std::chrono::steady_clock> fragmentEnd = std::chrono::high_resolution_clock::now();

		vertexTime += std::chrono::duration<float, std::milli>(vertexEnd - start).count();
		binningTime += std::chrono::duration<float, std::milli>(binningEnd - vertexEnd).count();
		fragmentTime += std::chrono::duration<float, std::milli>(fragmentEnd - binningEnd).count();

		currentPhase.store(Phase::Idle, std::memory_order_release);
	}

	const std::chrono::time_point<std::chrono::steady_clock> frameEnd = std::chrono::high_resolution_clock::now();

	frameTime = std::chrono::duration<float, std::milli>(frameEnd - prevFrame).count();
	prevFrame = frameEnd;
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
			threadRunBinning();
			break;
		case Phase::Fragment:
			threadRunFragment(threadID);
			break;
		case Phase::Idle:
		case Phase::Shutdown:
			break;
		}

		phaseBarrier.arrive_and_wait();
	}
}

void Renderer::threadRunVertex(const uint32_t threadID) {
	uint32_t totalVertices = 0;
	for (const CommandBuffer::DrawCallBatchCommand::DrawCallData& drawCall : currentCommandBatch->drawCalls)
	{
		totalVertices += drawCall.vertexCount;
	}

	const uint32_t vertsPerThread = (totalVertices + cpuCount - 1) / cpuCount;
	const uint32_t vertIdx = threadID * vertsPerThread;
	const uint32_t vertNum = std::min(vertsPerThread, totalVertices - vertIdx);

	if (vertNum == 0)
		return;

	uint32_t dcIdx = 0;
	uint32_t dcStartVertices = 0;
	while (dcIdx < currentCommandBatch->drawCalls.size() && vertIdx >= dcStartVertices + currentCommandBatch->drawCalls[dcIdx].vertexCount)
	{
		dcStartVertices += currentCommandBatch->drawCalls[dcIdx].vertexCount;
		dcIdx++;
	}

	uint32_t currentIdx = vertIdx;
	uint32_t remainingVertices = vertNum;

	const size_t vertexStride = currentCommandBatch->vertexStride;
	const size_t vOutStride = currentCommandBatch->vOutStride;

	while (remainingVertices > 0 && dcIdx < currentCommandBatch->drawCalls.size())
	{
		const auto& drawCall = currentCommandBatch->drawCalls[dcIdx];

		const uint32_t localIdx = currentIdx - dcStartVertices;
		const uint32_t verticesToProcess = std::min(remainingVertices, drawCall.vertexCount - localIdx);

		uint8_t* dstGeometryBytes = geometryScratchpad.data() + static_cast<uintptr_t>(currentIdx) * vOutStride;

		if (drawCall.indexData != nullptr)
		{
			const uint32_t* indices = drawCall.indexData + localIdx;
			const uint8_t* vertexBase = static_cast<const uint8_t*>(drawCall.vertexData);

			for (uint32_t i = 0; i < verticesToProcess; ++i)
			{
				const uint32_t actualVertexIndex = indices[i];

				const uint8_t* srcVertexBytes = vertexBase + static_cast<uintptr_t>(actualVertexIndex) * vertexStride;

				currentCommandBatch->vertexShader(dstGeometryBytes, srcVertexBytes, drawCall.uniform);

				VOutBase* vOut = reinterpret_cast<VOutBase*>(dstGeometryBytes);
				vOut->drawcallID = dcIdx;

				const float oneOverW = 1.0f / vOut->position.w;
				vOut->position *= oneOverW;
				vOut->position.x = (vOut->position.x + 1.0f) * 0.5f * static_cast<float>(framesize.x);
				vOut->position.y = (vOut->position.y + 1.0f) * 0.5f * static_cast<float>(framesize.y);
				vOut->position.w = oneOverW;

				dstGeometryBytes += vOutStride;
			}
		}
		else
		{
			const uint8_t* srcVertexBytes = static_cast<const uint8_t*>(drawCall.vertexData) + static_cast<uintptr_t>(localIdx) * vertexStride;

			for (uint32_t i = 0; i < verticesToProcess; ++i)
			{
				currentCommandBatch->vertexShader(dstGeometryBytes, srcVertexBytes, drawCall.uniform);

				VOutBase* vOut = reinterpret_cast<VOutBase*>(dstGeometryBytes);
				vOut->drawcallID = dcIdx;

				const float oneOverW = 1.0f / vOut->position.w;
				vOut->position *= oneOverW;
				vOut->position.x = (vOut->position.x + 1.0f) * 0.5f * static_cast<float>(framesize.x);
				vOut->position.y = (vOut->position.y + 1.0f) * 0.5f * static_cast<float>(framesize.y);
				vOut->position.w = oneOverW;

				srcVertexBytes += vertexStride;
				dstGeometryBytes += vOutStride;
			}
		}

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
		return crossZ < 0.0f;
	}
	if (mode == PipelineState::CullMode::Front) 
	{
		return crossZ > 0.0f;
	}

	return false;
}

void Renderer::threadRunBinning()
{
	while (true)
	{
		const uint32_t nextTri = triangleCounter.fetch_add(1);
		const uint32_t triBytePos = nextTri * 3 * currentCommandBatch->vOutStride;

		if (triBytePos >= geometryScratchpad.size())
			break;

		const VOutBase* v1 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 0 * currentCommandBatch->vOutStride]);
		const VOutBase* v2 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 1 * currentCommandBatch->vOutStride]);
		const VOutBase* v3 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 2 * currentCommandBatch->vOutStride]);

		if (v1->position.w <= 0.0f || !std::isfinite(v1->position.w) ||
			v2->position.w <= 0.0f || !std::isfinite(v2->position.w) ||
			v3->position.w <= 0.0f || !std::isfinite(v3->position.w))
		{
			continue;
		}

		if (cullTriangle(v1->position, v2->position, v3->position, currentCommandBatch->state.cullMode))
		{
			continue;
		}

		const glm::vec2 bboxMin = glm::min(glm::min(v1->position, v2->position), v3->position);
		const glm::vec2 bboxMax = glm::max(glm::max(v1->position, v2->position), v3->position);

		const glm::uvec2 maxTileIndex = framesize / 16u - glm::uvec2(1u);
		const glm::uvec2 tileMin = glm::clamp(glm::uvec2(bboxMin / 16.0f), glm::uvec2(0u), maxTileIndex);
		const glm::uvec2 tileMax = glm::clamp(glm::uvec2(bboxMax / 16.0f), glm::uvec2(0u), maxTileIndex);

		for (uint32_t y = tileMin.y; y <= tileMax.y; ++y)
		{
			for (uint32_t x = tileMin.x; x <= tileMax.x; ++x)
			{
				const uint32_t nodeIdx = binningCounter.fetch_add(1, std::memory_order_relaxed);
				BinNode& node = binningScratchpad[nodeIdx];
				node.triangleID = nextTri;
				node.uniforms = currentCommandBatch->drawCalls[v1->drawcallID].uniform;

				const uint32_t tileIdx = y * tileRowSize + x;
				Tile& tile = tiles[tileIdx];
				const uint32_t oldHead = tile.head.exchange(nodeIdx, std::memory_order_acq_rel);
				node.next = oldHead;
				tile.count.fetch_add(1, std::memory_order_acq_rel);
			}
		}
	}
}

void Renderer::threadRunFragment(const uint32_t threadID)
{
	std::vector<BinNode> localBinNodes;
	while (true)
	{
		const uint32_t tileIdx = tileCounter.fetch_add(1);

		if (tileIdx >= tiles.size())
			break;

		Tile& tile = tiles[tileIdx];
		glm::ivec2 tileCoords = glm::ivec2((tileIdx) % tileRowSize, (tileIdx) / tileRowSize) * 16;
		const uint32_t triangleCount = tile.count.load(std::memory_order_acquire);
		
		localBinNodes.clear();
		localBinNodes.reserve(triangleCount);
		
		uint32_t nodeIdx = tile.head.load(std::memory_order_acquire);
		while (nodeIdx != UINT32_MAX)
		{
			localBinNodes.push_back(binningScratchpad[nodeIdx]);
			nodeIdx = binningScratchpad[nodeIdx].next;
		}

		std::ranges::sort(localBinNodes, [this](const BinNode& a, const BinNode& b) { return a.triangleID < b.triangleID; });

		for (const BinNode& node : localBinNodes)
		{
			const uint32_t triBytePos = node.triangleID * 3 * currentCommandBatch->vOutStride;
			const VOutBase* v1 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 0 * currentCommandBatch->vOutStride]);
			const VOutBase* v2 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 1 * currentCommandBatch->vOutStride]);
			const VOutBase* v3 = reinterpret_cast<const VOutBase*>(&geometryScratchpad[triBytePos + 2 * currentCommandBatch->vOutStride]);
			
			const glm::vec2 p1 = glm::vec2(v1->position);
			const glm::vec2 p2 = glm::vec2(v2->position);
			const glm::vec2 p3 = glm::vec2(v3->position);

			const glm::ivec2 bboxMin = glm::min(glm::min(p1, p2), p3);
			const glm::ivec2 bboxMax = glm::max(glm::max(p1, p2), p3);

			const glm::ivec2 start = glm::clamp(bboxMin, tileCoords, tileCoords + glm::ivec2(15));
			const glm::ivec2 end = glm::clamp(bboxMax, tileCoords, tileCoords + glm::ivec2(15));

			const float triangleArea = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
			if (std::abs(triangleArea) < 0.0001f) 
				continue;

			float invArea = 1.0f / triangleArea;

			for (int32_t y = start.y; y <= end.y; ++y)
			{
				for (int32_t x = start.x; x <= end.x; ++x)
				{
					glm::vec2 pixelCenter = glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
					float w0 = ((p3.x - p2.x) * (pixelCenter.y - p2.y) - (p3.y - p2.y) * (pixelCenter.x - p2.x)) * invArea;
					float w1 = ((p1.x - p3.x) * (pixelCenter.y - p3.y) - (p1.y - p3.y) * (pixelCenter.x - p3.x)) * invArea;
					float w2 = 1.0f - w0 - w1;

					if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
						continue;

					float depth = w0 * v1->position.z + w1 * v2->position.z + w2 * v3->position.z;
					uint32_t pixelIdx = y * framesize.x + x;
					if (currentCommandBatch->state.depthTest && depthBuffer[pixelIdx] <= depth)
						continue;

					void* interpolatedOutput = localVOutData.data() + static_cast<uintptr_t>(threadID) * currentCommandBatch->vOutStride;
					currentCommandBatch->interpolationShader(interpolatedOutput, v1, v2, v3, glm::vec3(w0, w1, w2), node.uniforms);
					const glm::vec4 color = currentCommandBatch->fragmentShader(interpolatedOutput, node.uniforms);
					if (currentCommandBatch->blendShader != nullptr)
					{
						glm::vec4 backgroundColor = glm::vec4(framebuffer[pixelIdx]) / 255.0f;
						glm::vec4 blendedColor = currentCommandBatch->blendShader(&color, &backgroundColor);
						glm::vec4 clampedColor = glm::clamp(blendedColor, 0.0f, 1.0f);
						framebuffer[pixelIdx] = glm::u8vec4(clampedColor * 255.0f);
					}
					else
					{
						framebuffer[pixelIdx] = glm::u8vec4(glm::clamp(color, 0.0f, 1.0f) * 255.0f);
					}

					if (currentCommandBatch->state.depthWrite)
						depthBuffer[pixelIdx] = depth;
				}
			}
		}
	}
}

void Renderer::initTiles()
{
	tiles.clear();
	const uint32_t tileCount = (framesize.x / 16) * (framesize.y / 16);
	tiles.resize(tileCount);
	tileRowSize = framesize.x / 16;
}
