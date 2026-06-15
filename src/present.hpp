#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <glm.hpp>

template<typename T>
concept Output = requires(T t) {
	t.getSize() == std::declval<glm::uvec2>();
    t.present();
	t.getDepth() == std::declval<float*>();
	t.getColor() == std::declval<glm::u8vec4*>();
	t.clear(std::declval<glm::u8vec4>());
};

class TerminalCanvas 
{
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

    void present() const {
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

        std::cout.write(outputStrBuffer.data(), outputStrBuffer.size());
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