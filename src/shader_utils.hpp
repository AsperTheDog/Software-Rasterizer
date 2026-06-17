#pragma once
#include <glm.hpp>
#include <gtc/color_space.hpp>

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
}
