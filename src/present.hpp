#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <glm.hpp>

#include "camera.hpp"
#include "Texture.hpp"

template<typename T>
concept Output = requires(T t, Texture<glm::u8vec4>* tex, float vertexTime, float binningTime, float fragmentTime, float frameTime, Camera& cam) {
	t.getSize() == std::declval<glm::uvec2>();
    t.present(tex, vertexTime, binningTime, fragmentTime, frameTime, cam);
};

struct PerformanceData
{
	float vertexTime;
	float binningTime;
	float fragmentTime;
	float frameTime;
};

class TerminalCanvas 
{
    mutable float presentTime = 0.0f;

public:
	TerminalCanvas(const uint32_t w, const uint32_t h) : width(w), height(h)
	{
        outputStrBuffer.reserve(3 + (width * height * 24) + (height * 5));
    }

    [[nodiscard]] uint32_t getWidth() const { return width; }
    [[nodiscard]] uint32_t getHeight() const { return height; }

    void present(Texture<glm::u8vec4>& tex, const PerformanceData& perfData, const Camera& cam) const {
        
		const auto start = std::chrono::steady_clock::now();

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
                const glm::u8vec3 c = tex.getPixel({x, y}, false);

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
        outputStrBuffer.append(std::to_string(perfData.vertexTime));
        outputStrBuffer.append(" ms\n");    

        outputStrBuffer.append("Binning: ");
		outputStrBuffer.append(std::to_string(perfData.binningTime));
		outputStrBuffer.append(" ms\n");

        outputStrBuffer.append("Fragment: ");
		outputStrBuffer.append(std::to_string(perfData.fragmentTime));
		outputStrBuffer.append(" ms\n");

        outputStrBuffer.append("Present: ");
        outputStrBuffer.append(std::to_string(presentTime));
        outputStrBuffer.append(" ms\n");

		outputStrBuffer.append("Frame: ");
		outputStrBuffer.append(std::to_string(perfData.frameTime));
		outputStrBuffer.append(" ms\n");
        outputStrBuffer.append("FPS: ");
		outputStrBuffer.append(std::to_string(static_cast<uint32_t>(1000.0f / perfData.frameTime)));
        outputStrBuffer.append(" fps\n");

        std::cout.write(outputStrBuffer.data(), outputStrBuffer.size());

        const auto end = std::chrono::steady_clock::now();
        presentTime = std::chrono::duration<float, std::milli>(end - start).count();
    }

    glm::uvec2 getSize() const
    {
		return {width, height};
    }

private:
    uint32_t width;
    uint32_t height;
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

    void present(Texture<glm::u8vec4>& tex, const PerformanceData& perfData, const Camera& cam) const
    {
        if (tex.isSwizzled())
			throw std::runtime_error("Cannot present a swizzled texture. Please disable swizzling when creating the texture.");

        const std::string perfStats = "Software Rasterizer | VS: " + std::to_string(perfData.vertexTime) +
            "ms | Bin: " + std::to_string(perfData.binningTime) +
            "ms | FS: " + std::to_string(perfData.fragmentTime) +
            "ms | Frame: " + std::to_string(perfData.frameTime) + "ms" +
            " | Camera: " + std::to_string(cam.getPosition().x) + ", " + std::to_string(cam.getPosition().y) + ", " + std::to_string(cam.getPosition().z);
        SDL_SetWindowTitle(window, perfStats.c_str());

        SDL_UpdateTexture(texture, nullptr, tex.data(), static_cast<int>(size.x * sizeof(glm::u8vec4)));

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

    bool isWindowOpen = true;

    bool isMouseCaptured = false;
};

#endif