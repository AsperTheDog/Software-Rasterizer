#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <glm.hpp>
#include <gtc/color_space.hpp>
#include <limits>

struct VOutBase {
    glm::vec4 position;
    glm::vec4 clipPosition;
};

namespace ShaderUtils {

    template<typename T>
    [[nodiscard]] constexpr T interpolateLinear(const T& a, const T& b, const T& c, const glm::vec3& weights)
    {
        return (a * weights.x) + (b * weights.y) + (c * weights.z);
    }

    template<typename T, typename V>
    [[nodiscard]] constexpr T interpolatePerspective(const T& a, const T& b, const T& c, const glm::vec3& weights, const V* v1, const V* v2, const V* v3)
    {
        static_assert(std::derived_from<V, VOutBase>, "Vertices must derive from VOutBase");

        const float invW1 = v1->position.w;
        const float invW2 = v2->position.w;
        const float invW3 = v3->position.w;

        const float interpolatedInvW = (invW1 * weights.x) + (invW2 * weights.y) + (invW3 * weights.z);

        if (std::abs(interpolatedInvW) < 1e-6f) [[unlikely]]
        {
            return T{};
        }

        const T attributeStep = (a * invW1 * weights.x) + (b * invW2 * weights.y) + (c * invW3 * weights.z);

        return attributeStep / interpolatedInvW;
    }

    [[nodiscard]] inline glm::vec4 srgbToLinear(const glm::vec4& srgbColor) noexcept
    {
        return glm::convertSRGBToLinear(srgbColor);
    }

    [[nodiscard]] inline glm::vec4 linearToSrgb(const glm::vec4& linearColor) noexcept
    {
        return glm::convertLinearToSRGB(linearColor);
    }

    namespace detail
    {
        [[nodiscard]] inline float encodeSrgbByte(const float linear) noexcept
        {
            const float srgb = linearToSrgb(glm::vec4(linear)).x;
            return glm::round(glm::clamp(srgb, 0.0f, 1.0f) * 255.0f);
        }

        constexpr uint32_t encodeBucketCount = 4096;

        struct SrgbTables
        {
            std::array<float, 256> byteToLinear;
            std::array<float, 256> encodeThresholds;
            std::array<uint8_t, encodeBucketCount + 1> encodeBuckets;

            SrgbTables()
            {
                for (uint32_t byte = 0; byte < 256; ++byte)
                    byteToLinear[byte] = srgbToLinear(glm::vec4(static_cast<float>(byte) / 255.0f)).x;

                for (uint32_t byte = 1; byte < 256; ++byte)
                {
                    uint32_t low = 0;
                    uint32_t high = std::bit_cast<uint32_t>(1.0f);
                    while (low < high)
                    {
                        const uint32_t mid = low + (high - low) / 2;
                        if (encodeSrgbByte(std::bit_cast<float>(mid)) >= static_cast<float>(byte))
                            high = mid;
                        else
                            low = mid + 1;
                    }
                    encodeThresholds[byte - 1] = std::bit_cast<float>(low);
                }
                encodeThresholds[255] = std::numeric_limits<float>::infinity();

                uint32_t byte = 0;
                for (uint32_t bucket = 0; bucket <= encodeBucketCount; ++bucket)
                {
                    const float bucketStart = static_cast<float>(bucket) / static_cast<float>(encodeBucketCount);
                    while (encodeThresholds[byte] <= bucketStart)
                        ++byte;
                    encodeBuckets[bucket] = static_cast<uint8_t>(byte);
                }
            }
        };

        inline const SrgbTables srgbTables;
    }

    [[nodiscard]] inline glm::vec4 srgb8ToLinear(const glm::vec4& srgbBytes) noexcept
    {
        return glm::vec4(
            detail::srgbTables.byteToLinear[static_cast<uint8_t>(srgbBytes.x)],
            detail::srgbTables.byteToLinear[static_cast<uint8_t>(srgbBytes.y)],
            detail::srgbTables.byteToLinear[static_cast<uint8_t>(srgbBytes.z)],
            srgbBytes.w / 255.0f);
    }

    [[nodiscard]] inline glm::vec4 linearToSrgb8(const glm::vec4& linearColor) noexcept
    {
        glm::vec4 bytes;
        for (glm::length_t channel = 0; channel < 3; ++channel)
        {
            const float linear = std::min(std::max(0.0f, linearColor[channel]), 1.0f);
            uint32_t byte = detail::srgbTables.encodeBuckets[static_cast<uint32_t>(linear * static_cast<float>(detail::encodeBucketCount))];
            byte += detail::srgbTables.encodeThresholds[byte] <= linear ? 1 : 0;
            bytes[channel] = static_cast<float>(byte);
        }
        bytes.w = glm::round(glm::clamp(linearColor.w, 0.0f, 1.0f) * 255.0f);
        return bytes;
    }
}
