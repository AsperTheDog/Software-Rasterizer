#pragma once
#include <algorithm>
#include <glm.hpp>
#include <string_view>

#include <stb_image.h>
#include <stdexcept>

#include "shader_utils.hpp"

template<typename T>
concept PixelFormat = requires(T a) {
    typename T::value_type;
    typename T::length_type;
	{ glm::vec4(a) } -> std::same_as<glm::vec4>;
};

[[nodiscard]] constexpr uint32_t expandBits(uint32_t v) noexcept
{
    v &= 0x0000ffff;                  // v = ---- ---- ---- ---- fedc ba98 7654 3210
    v = (v ^ (v << 8)) & 0x00ff00ff;  // v = ---- ---- fedc ba98 ---- ---- 7654 3210
    v = (v ^ (v << 4)) & 0x0f0f0f0f;  // v = ---- fedc ---- ba98 ---- 7654 ---- 3210
    v = (v ^ (v << 2)) & 0x33333333;  // v = --fe --dc --ba --98 --76 --54 --32 --10
    v = (v ^ (v << 1)) & 0x55555555;  // v = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
    return v;
}

enum Filter : uint8_t { NEAREST, LINEAR };
enum Border : uint8_t { CLAMP, REPEAT };
enum Format : uint8_t { SRGB, UNORM };

template<PixelFormat Pixel>
class Texture {
public:

    Texture(glm::uvec2 size, Pixel defaultValue, const bool disableSwizzling = false)
    {
		this->size = size;
        if (!disableSwizzling && size.x == size.y && (size.x & (size.x - 1)) == 0) 
        {
			swizzled = true;
		}
		pixels.resize(static_cast<size_t>(size.x) * size.y, defaultValue);
    }

    explicit Texture(const std::string_view filename, const bool disableSwizzling = false)
    {
        int w, h, actualChannels;

        unsigned char* rawData = stbi_load(filename.data(), &w, &h, &actualChannels, 4);

        if (!rawData) 
        {
            throw std::runtime_error("Failed to load texture: " + std::string(filename) +
                "\nReason: " + stbi_failure_reason());
        }

        size.x = static_cast<uint32_t>(w);
        size.y = static_cast<uint32_t>(h);

        const size_t totalPixels = static_cast<size_t>(size.x) * size.y;
        pixels.resize(totalPixels);

        if (!disableSwizzling && size.x == size.y && (size.x & (size.x - 1)) == 0) 
        {
            swizzled = true;
            const glm::u8vec4* linearPixels = reinterpret_cast<glm::u8vec4*>(rawData);
            for (uint32_t y = 0; y < size.y; ++y) 
            {
                for (uint32_t x = 0; x < size.x; ++x) 
                {
                    const uint32_t linearIndex = y * size.x + x;
                    const uint32_t mortonIndex = getPixelIndex({ x, y });
                    pixels[mortonIndex] = linearPixels[linearIndex];
                }
            }
        }
        else
        {
            std::memcpy(pixels.data(), rawData, totalPixels * sizeof(glm::u8vec4));
        }


        stbi_image_free(rawData);
    }

    [[nodiscard]] size_t getPixelIndex(const glm::uvec2 coord) const
    {
        if (swizzled)
            return expandBits(coord.y) << 1 | expandBits(coord.x);
        return coord.y * size.x + coord.x;
    }

    void setSampler(const Filter newFilter, const Border newBorder)
    {
        filter = newFilter;
        border = newBorder;
    }

	void setFormat(const Format newFormat) { format = newFormat; }

    [[nodiscard]] glm::vec4 sample(glm::vec2 uv, bool normalized = true) const
    {
        if (filter == NEAREST)
        {
            uint32_t x, y;
            if (border == CLAMP)
            {
                uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
                x = static_cast<uint32_t>(uv.x * (size.x - 1));
                y = static_cast<uint32_t>(uv.y * (size.y - 1));
            }
            else
            {
                uv = glm::mod(uv, glm::vec2(1.0f));
                if (uv.x < 0.0f) uv.x += 1.0f;
                if (uv.y < 0.0f) uv.y += 1.0f;
                x = static_cast<uint32_t>(uv.x * size.x) % size.x;
                y = static_cast<uint32_t>(uv.y * size.y) % size.y;
            }
            return decodeSample(pixels[getPixelIndex({ x, y })], normalized);
        }

        const float u = uv.x * size.x - 0.5f;
        const float v = uv.y * size.y - 0.5f;

        const int x0 = static_cast<int>(std::floor(u));
        const int y0 = static_cast<int>(std::floor(v));

        const int x1 = x0 + 1;
        const int y1 = y0 + 1;

        const float wx = u - std::floor(u);
        const float wy = v - std::floor(v);

        uint32_t px0, px1, py0, py1;

        if (border == CLAMP)
        {
            px0 = glm::clamp(x0, 0, static_cast<int>(size.x - 1));
            px1 = glm::clamp(x1, 0, static_cast<int>(size.x - 1));
            py0 = glm::clamp(y0, 0, static_cast<int>(size.y - 1));
            py1 = glm::clamp(y1, 0, static_cast<int>(size.y - 1));
        }
        else
        {
            px0 = (x0 % static_cast<int>(size.x) + size.x) % size.x;
            px1 = (x1 % static_cast<int>(size.x) + size.x) % size.x;
            py0 = (y0 % static_cast<int>(size.y) + size.y) % size.y;
            py1 = (y1 % static_cast<int>(size.y) + size.y) % size.y;
        }

        const glm::vec4 c00 = decodeSample(glm::vec4(pixels[getPixelIndex({ px0, py0 })]), normalized);
        const glm::vec4 c10 = decodeSample(glm::vec4(pixels[getPixelIndex({ px1, py0 })]), normalized);
        const glm::vec4 c01 = decodeSample(glm::vec4(pixels[getPixelIndex({ px0, py1 })]), normalized);
        const glm::vec4 c11 = decodeSample(glm::vec4(pixels[getPixelIndex({ px1, py1 })]), normalized);

        const glm::vec4 top = glm::mix(c00, c10, wx);
        const glm::vec4 bottom = glm::mix(c01, c11, wx);
        return glm::mix(top, bottom, wy);
    }

    glm::vec4 getPixel(const glm::uvec2 coords, const bool quantize = true)
    {
		glm::vec4 pixel = glm::vec4(pixels[getPixelIndex(coords)]);
        if constexpr (!std::is_floating_point_v<typename Pixel::value_type>)
        {
	        if (quantize)
	        {
	        	const float elemSize = std::numeric_limits<typename Pixel::value_type>::max();
	        	pixel = pixel / elemSize;
	        }
        }

        if (quantize && format == SRGB)
            pixel = ShaderUtils::srgbToLinear(pixel);
		return pixel;
    }

    void setPixel(const glm::uvec2 coord, glm::vec4 color, const bool quantize = true, const Format srcFormat = UNORM)
    {
        if (srcFormat == SRGB && format == UNORM)
        {
            color = ShaderUtils::srgbToLinear(color);
        }
        else if (srcFormat == UNORM && format == SRGB)
        {
            color = ShaderUtils::linearToSrgb(color);
        }

        const uint32_t index = getPixelIndex(coord);
        
        if (quantize)
        {
            const glm::vec4 saturatedColor = glm::clamp(color, 0.0f, 1.0f);
            if constexpr (std::is_floating_point_v<typename Pixel::value_type>)
            {
                pixels[index] = Pixel(saturatedColor);
            }
            else
            {
                const float elemSize = std::numeric_limits<typename Pixel::value_type>::max();
                pixels[index] = Pixel(glm::round(saturatedColor * elemSize));
            }
        }
        else
        {
            pixels[index] = Pixel(color);
        }
    }

	Pixel* data()
	{
		return pixels.data();
	}

	[[nodiscard]] Pixel& at(const glm::uvec2 coord) { return pixels[getPixelIndex(coord)]; }
	[[nodiscard]] const Pixel& at(const glm::uvec2 coord) const { return pixels[getPixelIndex(coord)]; }

	[[nodiscard]] glm::uvec2 getSize() const
	{
		return size;
	}

    [[nodiscard]] bool isSwizzled() const
	{
		return swizzled;
	}

    void clear(Pixel value)
    {
		std::ranges::fill(pixels, value);
    }

private:
    static glm::vec4 normalizePixel(const glm::vec4 color, const bool normalized)
    {
        if constexpr (!std::is_floating_point_v<typename Pixel::value_type>)
            if (normalized)
                return color / static_cast<float>(std::numeric_limits<typename Pixel::value_type>::max());
        return color;
    }

    [[nodiscard]] glm::vec4 decodeSample(glm::vec4 color, const bool normalized) const
    {
        color = normalizePixel(color, normalized);
        if (normalized && format == SRGB)
            color = ShaderUtils::srgbToLinear(color);
        return color;
    }

	Filter filter = NEAREST;
	Border border = CLAMP;
	Format format = UNORM;
    glm::uvec2 size;
    std::vector<Pixel> pixels;
	bool swizzled = false;
};
