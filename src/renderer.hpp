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

	template<Output T>
	void attachOutput(T& output)
	{
		depthBuffer = output.getDepth();
		framebuffer = output.getColor();
		framesize = output.getSize();

		initTiles();
	}

private:
	enum class Phase: uint8_t { Idle, Vertex, Binning, Fragment, Shutdown };

	struct BinNode {
		uint32_t triangleID;
		uint32_t next;
		const void* uniforms;
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
	void threadRunFragment(uint32_t threadID);

	void initTiles();

	glm::uvec2 framesize;

	float* depthBuffer = nullptr;
	glm::u8vec4* framebuffer = nullptr;
	std::vector<uint8_t> geometryScratchpad;
	std::vector<BinNode> binningScratchpad;
	std::vector<uint8_t> localVOutData;
	std::vector<Tile> tiles;
	uint32_t tileRowSize = 0;

	std::atomic<uint32_t> triangleCounter{ 0 };
	std::atomic<uint32_t> binningCounter{ 0 };
	std::atomic<uint32_t> tileCounter{ 0 };

	uint32_t cpuCount = 1;
	std::vector<std::jthread> threads;
	std::atomic<Phase> currentPhase;
	std::barrier<> phaseBarrier;

	const CommandBuffer::DrawCallBatchCommand* currentCommandBatch = nullptr;
};

