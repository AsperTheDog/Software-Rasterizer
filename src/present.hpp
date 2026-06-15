#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <glm.hpp>

#include "camera.hpp"

template<typename T>
concept Output = requires(T t, float vertexTime, float binningTime, float fragmentTime, float frameTime, Camera& cam) {
	t.getSize() == std::declval<glm::uvec2>();
    t.present(vertexTime, binningTime, fragmentTime, frameTime, cam);
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
        : size(width, height)
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) 
        {
            throw std::runtime_error("Failed to initialize SDL3: " + std::string(SDL_GetError()));
        }

        window = SDL_CreateWindow(windowTitle.c_str(), static_cast<int>(width), static_cast<int>(height), 0);
        if (!window)
        {
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 window: " + std::string(SDL_GetError()));
        }

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) 
        {
            SDL_DestroyWindow(window);
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 renderer: " + std::string(SDL_GetError()));
        }

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width), static_cast<int>(height));
        if (!texture) 
        {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            throw std::runtime_error("Failed to create SDL3 texture: " + std::string(SDL_GetError()));
        }

        const size_t totalPixels = static_cast<size_t>(width) * height;
        colorBuffer.resize(totalPixels, glm::u8vec4(0, 0, 0, 255));
        depthBuffer.resize(totalPixels, 1.0f);
    }

    ~SdlWindow()
    {
        if (texture) 
            SDL_DestroyTexture(texture);

        if (renderer) 
            SDL_DestroyRenderer(renderer);

        if (window) 
            SDL_DestroyWindow(window);

        SDL_Quit();
    }

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    SdlWindow(SdlWindow&&) noexcept = default;
    SdlWindow& operator=(SdlWindow&&) noexcept = default;

    void processInput(Camera& camera)
    {
        SDL_Event l_Event;
        while (SDL_PollEvent(&l_Event))
        {
            switch (l_Event.type)
            {
            case SDL_EVENT_QUIT:
            {
                isWindowOpen = false;
            }
            break;
            case SDL_EVENT_MOUSE_MOTION:
            {
                camera.mouseMoved(l_Event.motion.xrel, l_Event.motion.yrel);
            }
            break;
            case SDL_EVENT_KEY_DOWN:
            {
                camera.keyPressed(l_Event.key.key);
            }
            break;
            case SDL_EVENT_MOUSE_WHEEL:
            {
                camera.mouseScrolled(l_Event.wheel.y);
            }
            break;
            case SDL_EVENT_KEY_UP:
            {
                camera.keyReleased(l_Event.key.key);
            }
            break;
            default:;
            }
        }
    }

    [[nodiscard]] glm::uvec2 getSize() const noexcept
    {
        return size;
    }

    [[nodiscard]] float* getDepth() noexcept
    {
        return depthBuffer.data();
    }

    [[nodiscard]] glm::u8vec4* getColor() noexcept
    {
        return colorBuffer.data();
    }

    void clear(const glm::u8vec4 clearColor = glm::u8vec4(0, 0, 0, 255)) noexcept
    {
        std::ranges::fill(colorBuffer, clearColor);
        std::ranges::fill(depthBuffer, std::numeric_limits<float>::infinity());
    }

    void present(const float vertexTime, const float binningTime, const float fragmentTime, const float frameTime, Camera& cam) noexcept
    {
        const std::string perfStats = "Software Rasterizer | VS: " + std::to_string(vertexTime) +
            "ms | Bin: " + std::to_string(binningTime) +
            "ms | FS: " + std::to_string(fragmentTime) +
            "ms | Frame: " + std::to_string(frameTime) + "ms" +
            " | Camera: " + std::to_string(cam.getPosition().x) + ", " + std::to_string(cam.getPosition().y) + ", " + std::to_string(cam.getPosition().z);
        SDL_SetWindowTitle(window, perfStats.c_str());

        SDL_UpdateTexture(texture, nullptr, colorBuffer.data(), static_cast<int>(size.x * sizeof(glm::u8vec4)));

        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return isWindowOpen;
    }

    void toggleMouseCaptured()
    {
        isMouseCaptured = !isMouseCaptured;
        SDL_SetWindowRelativeMouseMode(window, isMouseCaptured);

        if (isMouseCaptured)
        {
            SDL_ShowCursor();
        }
        else
        {
            SDL_HideCursor();
        }
    }

private:
    glm::uvec2 size;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    std::vector<glm::u8vec4> colorBuffer;
    std::vector<float> depthBuffer;
    bool isWindowOpen = true;

    bool isMouseCaptured = false;
};

#endif