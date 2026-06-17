#pragma once
#include <algorithm>
#include <bit>
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

enum Filter : uint8_t { NEAREST, BILINEAR, TRILINEAR };
enum Border : uint8_t { CLAMP, REPEAT };
enum Format : uint8_t { SRGB, UNORM };

template<PixelFormat Pixel>
class MipTexture;

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

    explicit Texture(Texture& tex, uint32_t downsample)
    {
        assert(tex.swizzled && "Texture must be swizzled (POT & Square) to downsample.");

        const uint32_t srcW = tex.getSize().x;
        const uint32_t srcH = tex.getSize().y;

        if (downsample == 0)
        {
            *this = tex;
            return;
        }

        uint32_t maxDownsample = 0;
        while ((srcW >> (maxDownsample + 1)) > 0)
        {
            maxDownsample++;
        }

        downsample = std::min(downsample, maxDownsample);

        const uint32_t newWidth = srcW >> downsample;
        const uint32_t newHeight = srcH >> downsample;

        this->size = glm::uvec2(newWidth, newHeight);
        this->swizzled = true;
        this->pixels.resize(static_cast<size_t>(newWidth) * newHeight);

        const Pixel* srcData = tex.data();
        const size_t blockSize = static_cast<size_t>(1) << (2 * downsample);
        const float divisor = static_cast<float>(blockSize);

        for (size_t dstIdx = 0; dstIdx < this->pixels.size(); ++dstIdx)
        {
            size_t srcStartIdx = dstIdx * blockSize;

            glm::vec4 accumulatedColor = glm::vec4(srcData[srcStartIdx]);
            for (size_t i = 1; i < blockSize; ++i)
            {
                accumulatedColor += glm::vec4(srcData[srcStartIdx + i]);
            }

            this->pixels[dstIdx] = accumulatedColor / divisor;
        }
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

        if (filter == TRILINEAR)
			filter = BILINEAR;
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
	        	constexpr float elemSize = std::numeric_limits<typename Pixel::value_type>::max();
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
                constexpr float elemSize = std::numeric_limits<typename Pixel::value_type>::max();
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

	friend class MipTexture<Pixel>;
};

template<PixelFormat Pixel>
class MipTexture
{
public:
    explicit MipTexture(const std::string_view filename, const float mipBias = 0.f) : lodBias(mipBias)
    {
        int w, h, actualChannels;

        const unsigned char* rawData = stbi_load(filename.data(), &w, &h, &actualChannels, 4);

        if (!rawData)
        {
            throw std::runtime_error("Failed to load texture: " + std::string(filename) +
                "\nReason: " + stbi_failure_reason());
        }

        size.x = static_cast<uint32_t>(w);
        size.y = static_cast<uint32_t>(h);

        stbi_image_free(const_cast<unsigned char*>(rawData));

		if (size.x != size.y || (size.x & (size.x - 1)) != 0)
		{
			throw std::runtime_error("MipTexture must be square and power of two in size.");
		}

        uint32_t mipCount = std::bit_width(std::max(size.x, size.y));
        mipmaps.reserve(mipCount);
        mipmaps.emplace_back(filename);

		for (uint32_t i = 1; i < mipCount; ++i)
		{
            mipmaps.emplace_back(mipmaps[i - 1], 1);
		}
    }

	void setSampler(const Filter newFilter, const Border newBorder)
	{
		trilinear = newFilter == TRILINEAR;
		for (Texture<Pixel>& mip : mipmaps)
		{
			mip.setSampler(newFilter, newBorder);
		}
	}

	void setMipBias(const float bias)
	{
		lodBias = bias;
	}

    void toggleDebugMode()
    {
		debugMode = !debugMode;
    }

    glm::vec4 sample(glm::vec2 uv, float tpw, const float mult, bool normalized = true)
    {
		uv *= mult;
		tpw *= mult;

        const float texelsPerPixel = tpw * size.x;
		const float maxMipLevel = static_cast<float>(mipmaps.size() - 1);
		const float mipLevel = std::min(calculateMipLevel(texelsPerPixel), maxMipLevel);
        if (debugMode)
        {
			const uint32_t mipIndex = std::min(static_cast<uint32_t>(std::round(mipLevel)), static_cast<uint32_t>(mipmaps.size() - 1));
            return { debugColors[mipIndex % std::size(debugColors)], 1.0f };
        }

		const float frac = glm::fract(mipLevel);

        if (trilinear && frac != 0.0f)
        {
			const uint32_t lowerMip = static_cast<uint32_t>(std::floor(mipLevel));
			const uint32_t upperMip = std::min(lowerMip + 1, static_cast<uint32_t>(mipmaps.size() - 1));
			const glm::vec4 lowerSample = mipmaps[lowerMip].sample(uv, normalized);
			const glm::vec4 upperSample = mipmaps[upperMip].sample(uv, normalized);
			return glm::mix(lowerSample, upperSample, frac);
		}

		const uint32_t mipIndex = std::min(static_cast<uint32_t>(std::round(mipLevel)), static_cast<uint32_t>(mipmaps.size() - 1));
		return mipmaps[mipIndex].sample(uv, normalized);
    }

private:
    [[nodiscard]] float calculateMipLevel(const float texelsPerPixel) const {
        if (texelsPerPixel <= 1.0f)
            return 0.0f;

        return std::max(0.0f, std::log2(texelsPerPixel) + lodBias);
    }

    std::vector<Texture<Pixel>> mipmaps;
    glm::uvec2 size;
    float lodBias = 0.f;
    bool trilinear = false;
    bool debugMode = false;

	static constexpr glm::vec3 debugColors[] = {
		glm::vec3(1.0f, 0.0f, 0.0f), // Red
		glm::vec3(0.0f, 1.0f, 0.0f), // Green
		glm::vec3(0.0f, 0.0f, 1.0f), // Blue
		glm::vec3(1.0f, 1.0f, 0.0f), // Yellow
		glm::vec3(1.0f, 0.0f, 1.0f), // Magenta
		glm::vec3(0.0f, 1.0f, 1.0f), // Cyan
		glm::vec3(1.0f, 1.0f, 1.0f)  // White
	};
};