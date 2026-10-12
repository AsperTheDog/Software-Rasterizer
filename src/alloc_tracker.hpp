#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#ifdef ALLOC_TRACKING

namespace alloc_tracker
{
	struct Snapshot
	{
		uint64_t allocs;
		uint64_t frees;
		uint64_t bytes;
	};

	inline std::atomic<uint64_t> gAllocs{ 0 };
	inline std::atomic<uint64_t> gFrees{ 0 };
	inline std::atomic<uint64_t> gBytes{ 0 };

	inline Snapshot snapshot()
	{
		return { gAllocs.load(std::memory_order_relaxed), gFrees.load(std::memory_order_relaxed), gBytes.load(std::memory_order_relaxed) };
	}

	class FrameReporter
	{
	public:
		explicit FrameReporter(uint32_t interval = 60) : interval(interval), last(snapshot()) {}

		void endFrame()
		{
			const Snapshot now = snapshot();
			const uint64_t a = now.allocs - last.allocs;
			const uint64_t f = now.frees - last.frees;
			const uint64_t b = now.bytes - last.bytes;
			last = now;

			sumA += a; sumF += f; sumB += b;
			if (a > maxA) maxA = a;
			if (++frames >= interval)
			{
				std::fprintf(stderr, "[alloc] last frame: %llu allocs, %llu frees, %llu bytes | avg over %u frames: %.1f allocs, %.1f frees, %.0f bytes | max allocs/frame: %llu\n",
					(unsigned long long)a, (unsigned long long)f, (unsigned long long)b,
					frames, double(sumA) / frames, double(sumF) / frames, double(sumB) / frames, (unsigned long long)maxA);
				frames = 0; sumA = sumF = sumB = maxA = 0;
			}
		}

	private:
		uint32_t interval;
		Snapshot last;
		uint32_t frames = 0;
		uint64_t sumA = 0, sumF = 0, sumB = 0, maxA = 0;
	};

	inline void* allocate(std::size_t size)
	{
		void* p = std::malloc(size ? size : 1);
		if (!p) throw std::bad_alloc{};
		gAllocs.fetch_add(1, std::memory_order_relaxed);
		gBytes.fetch_add(size, std::memory_order_relaxed);
		return p;
	}

	inline void* allocateAligned(std::size_t size, std::size_t align)
	{
#ifdef _WIN32
		void* p = _aligned_malloc(size ? size : 1, align);
#else
		void* p = std::aligned_alloc(align, (size + align - 1) / align * align);
#endif
		if (!p) throw std::bad_alloc{};
		gAllocs.fetch_add(1, std::memory_order_relaxed);
		gBytes.fetch_add(size, std::memory_order_relaxed);
		return p;
	}

	inline void release(void* p) noexcept
	{
		if (!p) return;
		gFrees.fetch_add(1, std::memory_order_relaxed);
		std::free(p);
	}

	inline void releaseAligned(void* p) noexcept
	{
		if (!p) return;
		gFrees.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
		_aligned_free(p);
#else
		std::free(p);
#endif
	}
}

void* operator new(std::size_t n) { return alloc_tracker::allocate(n); }
void* operator new[](std::size_t n) { return alloc_tracker::allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return alloc_tracker::allocate(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return alloc_tracker::allocate(n); } catch (...) { return nullptr; } }
void* operator new(std::size_t n, std::align_val_t a) { return alloc_tracker::allocateAligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return alloc_tracker::allocateAligned(n, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { alloc_tracker::release(p); }
void operator delete[](void* p) noexcept { alloc_tracker::release(p); }
void operator delete(void* p, std::size_t) noexcept { alloc_tracker::release(p); }
void operator delete[](void* p, std::size_t) noexcept { alloc_tracker::release(p); }
void operator delete(void* p, std::align_val_t) noexcept { alloc_tracker::releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alloc_tracker::releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alloc_tracker::releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alloc_tracker::releaseAligned(p); }

#else

namespace alloc_tracker
{
	struct FrameReporter
	{
		explicit FrameReporter(uint32_t = 60) {}
		void endFrame() {}
	};
}

#endif
