#pragma once
#include <barrier>
#include <glm.hpp>
#include <thread>
#include <vector>

#include "command_buffer.hpp"
#include "present.hpp"

class CommandBuffer;

class Renderer
{
public:
	Renderer();
	~Renderer();

	void execute(const CommandBuffer& commandBuffer);
	void endFrame();
	
	void setFramesize(const glm::uvec2 newSize, const uint32_t downsampleFactor = 1)
	{
		downsample = downsampleFactor;
		framesize = (newSize + glm::uvec2(downsampleFactor - 1u)) / downsampleFactor;
		initTiles();
	}

	[[nodiscard]] glm::uvec2 getFramesize() const { return framesize; }
	[[nodiscard]] uint32_t getDownsample() const { return downsample; }

	[[nodiscard]] float getVertexTime() const { return vertexTime; }
	[[nodiscard]] float getBinningTime() const { return binningTime; }
	[[nodiscard]] float getFragmentTime() const { return fragmentTime; }
	[[nodiscard]] float getFrameTime() const { return frameTime; }

private:
	enum class Phase: uint8_t { Idle, Vertex, Binning, Fragment, Shutdown, Compute };

	struct BinNode {
		uint32_t triangleID;
		uint32_t next;
		uint32_t v[3];
		const void* uniforms;
	};

	struct DrawInfo {
		uint32_t vertexBase;
		uint32_t triBase;
		uint32_t triCount;
		const uint32_t* indexData;
		const void* uniform;
	};

	struct Tile {
		std::atomic<uint32_t> head{UINT32_MAX};
		std::atomic<uint32_t> count{0};

		Tile() = default;

		Tile(Tile&&) noexcept {}

		Tile& operator=(Tile&&) noexcept {
			head.store(UINT32_MAX, std::memory_order_relaxed);
			count.store(0, std::memory_order_relaxed);
			return *this;
		}

		Tile(const Tile&) = delete;
		Tile& operator=(const Tile&) = delete;
	};

	void threadRun(const std::stop_token& stopToken, uint32_t threadID);
	void threadRunVertex(uint32_t threadID);
	void threadRunBinning();
	void threadRunFragment();

	void threadRunCompute();

	void initTiles();

	static constexpr uint32_t CULL_BIT = 0x80000000u;
	[[nodiscard]] const VOutBase* resolveVertex(uint32_t slot, uint32_t vOutStride) const;

	glm::uvec2 framesize;
	uint32_t downsample = 1;

	std::vector<uint8_t> geometryScratchpad;
	std::vector<uint8_t> cullGeomScratchpad;
	std::vector<uint8_t> clipcodes;
	std::vector<BinNode> binningScratchpad;
	std::vector<Tile> tiles;
	uint32_t tileRowSize = 0;

	std::vector<DrawInfo> drawInfos;
	uint32_t totalTriangles = 0;

	std::atomic<uint32_t> triangleCounter{ 0 };
	std::atomic<uint32_t> binningCounter{ 0 };
	std::atomic<uint32_t> binningCullCounter{ 0 };
	std::atomic<uint32_t> tileCounter{ 0 };
	std::atomic<uint32_t> computeCounter{ 0 };

	uint32_t cpuCount = 1;
	std::vector<std::jthread> threads;
	std::atomic<Phase> currentPhase;
	std::barrier<> phaseBarrier;

	const CommandBuffer::DrawCallBatchCommand* currentDrawCall = nullptr;
	const CommandBuffer::ComputeCommand* currentComputeCall = nullptr;

	double vertexTime = 0.0f, binningTime = 0.0f, fragmentTime = 0.0f, frameTime = 0.0f, computeTime = 0.0f;
	std::chrono::time_point<std::chrono::steady_clock> prevFrame;
};

