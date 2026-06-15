#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <glm.hpp>

template<typename T>
concept Output = requires(T t, float vertexTime, float binningTime, float fragmentTime, float frameTime) {
	t.getSize() == std::declval<glm::uvec2>();
    t.present(vertexTime, binningTime, fragmentTime, frameTime);
	t.getDepth() == std::declval<float*>();
	t.getColor() == std::declval<glm::u8vec4*>();
	t.clear(std::declval<glm::u8vec4>());
};

class TerminalCanvas 
{
    mutable float presentTime;

public:
	TerminalCanvas(const uint32_t w, const uint32_t h) : width(w), height(h), buffer(w* h, { 0, 0, 0, 255 }), depthBuffer(w* h, std::numeric_limits<float>::infinity())
	{
        outputStrBuffer.reserve(3 + (width * height * 24) + (height * 5));
    }

    [[nodiscard]] uint32_t getWidth() const { return width; }
    [[nodiscard]] uint32_t getHeight() const { return height; }

    void clear(const glm::u8vec4 clearColor = glm::u8vec4(0, 0, 0, 255)) 
	{
	    std::ranges::fill(buffer, clearColor);
		std::ranges::fill(depthBuffer, std::numeric_limits<float>::infinity());
    }

    void present(const float vertexTime, const float binningTime, const float fragmentTime, const float frameTime) const {

		auto start = std::chrono::steady_clock::now();

        outputStrBuffer.clear();
        outputStrBuffer.append("\x1b[H");

        char numBuff[4];
        auto append_num = [&numBuff](std::string& s, const uint8_t val) 
    	{
            auto [ptr, ec] = std::to_chars(numBuff, numBuff + 4, val);
            s.append(numBuff, ptr - numBuff);
    	};

        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const glm::u8vec3 c = buffer[y * width + x];

                outputStrBuffer.append("\x1b[48;2;");
                append_num(outputStrBuffer, c.r);
                outputStrBuffer.push_back(';');
                append_num(outputStrBuffer, c.g);
                outputStrBuffer.push_back(';');
                append_num(outputStrBuffer, c.b);
                outputStrBuffer.append("m  ");
            }
            outputStrBuffer.append("\x1b[0m\n");
        }

        outputStrBuffer.append("Timings:\n");
		
		outputStrBuffer.append("Vertex: ");
        outputStrBuffer.append(std::to_string(vertexTime));
        outputStrBuffer.append(" ms\n");    

        outputStrBuffer.append("Binning: ");
		outputStrBuffer.append(std::to_string(binningTime));
		outputStrBuffer.append(" ms\n");

        outputStrBuffer.append("Fragment: ");
		outputStrBuffer.append(std::to_string(fragmentTime));
		outputStrBuffer.append(" ms\n");

        outputStrBuffer.append("Present: ");
        outputStrBuffer.append(std::to_string(presentTime));
        outputStrBuffer.append(" ms\n");

		outputStrBuffer.append("Frame: ");
		outputStrBuffer.append(std::to_string(frameTime));
		outputStrBuffer.append(" ms\n");
        outputStrBuffer.append("FPS: ");
		outputStrBuffer.append(std::to_string(static_cast<uint32_t>(1000.0f / frameTime)));
        outputStrBuffer.append(" fps\n");

        std::cout.write(outputStrBuffer.data(), outputStrBuffer.size());

        const auto end = std::chrono::steady_clock::now();
        presentTime = std::chrono::duration<float, std::milli>(end - start).count();
    }

    glm::uvec2 getSize() const
    {
		return {width, height};
    }

	glm::u8vec4* getColor()
	{
		return buffer.data();
	}

	float* getDepth()
	{
		return depthBuffer.data();
	}

private:
    uint32_t width;
    uint32_t height;
    std::vector<glm::u8vec4> buffer;
	std::vector<float> depthBuffer;
    mutable std::string outputStrBuffer;
};

inline void initializeTerminal() {
    std::cout << "\x1b[?25l";
    std::cout << "\x1b[2J";
}

inline void restoreTerminal() {
    std::cout << "\x1b[?25h\x1b[0m\n";
}

#ifdef SDL_OUTPUT

#include <SDL3/SDL.h>

class SdlWindow
{
public:
    SdlWindow(const uint32_t width, const uint32_t height, const std::string& windowTitle = "Software Rasterizer")
        : m_size(width, height)
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) 
        {
            throw std::runtime_error("Failed to initialize SDL3: " + std::string(SDL_GetError()));
        }

        m_window = SDL_CreateWindow(windowTitle.c_str(), static_cast<int>(width), static_cast<int>(height), 0);
        if (!m_window)
        {
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 window: " + std::string(SDL_GetError()));
        }

        m_renderer = SDL_CreateRenderer(m_window, nullptr);
        if (!m_renderer) 
        {
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 renderer: " + std::string(SDL_GetError()));
        }

        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width), static_cast<int>(height));
        if (!m_texture) 
        {
            SDL_DestroyRenderer(m_renderer);
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 texture: " + std::string(SDL_GetError()));
        }

        const size_t totalPixels = static_cast<size_t>(width) * height;
        m_colorBuffer.resize(totalPixels, glm::u8vec4(0, 0, 0, 255));
        m_depthBuffer.resize(totalPixels, 1.0f);
    }

    ~SdlWindow()
    {
        if (m_texture) 
            SDL_DestroyTexture(m_texture);

        if (m_renderer) 
            SDL_DestroyRenderer(m_renderer);

        if (m_window) 
            SDL_DestroyWindow(m_window);

        SDL_Quit();
    }

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    SdlWindow(SdlWindow&&) noexcept = default;
    SdlWindow& operator=(SdlWindow&&) noexcept = default;

    [[nodiscard]] glm::uvec2 getSize() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] float* getDepth() noexcept
    {
        return m_depthBuffer.data();
    }

    [[nodiscard]] glm::u8vec4* getColor() noexcept
    {
        return m_colorBuffer.data();
    }

    void clear(const glm::u8vec4 clearColor = glm::u8vec4(0, 0, 0, 255)) noexcept
    {
        std::ranges::fill(m_colorBuffer, clearColor);
        std::ranges::fill(m_depthBuffer, std::numeric_limits<float>::infinity());
    }

    void present(const float vertexTime, const float binningTime, const float fragmentTime, const float frameTime) noexcept
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) 
        {
            if (event.type == SDL_EVENT_QUIT) 
            {
                m_isWindowOpen = false;
            }
        }

        const std::string perfStats = "Software Rasterizer | VS: " + std::to_string(vertexTime) +
            "ms | Bin: " + std::to_string(binningTime) +
            "ms | FS: " + std::to_string(fragmentTime) +
            "ms | Frame: " + std::to_string(frameTime) + "ms";
        SDL_SetWindowTitle(m_window, perfStats.c_str());

        SDL_UpdateTexture(m_texture, nullptr, m_colorBuffer.data(), static_cast<int>(m_size.x * sizeof(glm::u8vec4)));

        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_isWindowOpen;
    }

private:
    glm::uvec2 m_size;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_texture = nullptr;

    std::vector<glm::u8vec4> m_colorBuffer;
    std::vector<float> m_depthBuffer;
    bool m_isWindowOpen = true;
};

#endif