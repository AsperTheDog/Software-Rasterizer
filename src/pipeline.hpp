#pragma once
#include <glm.hpp>
#include <concepts>
#include <type_traits>

struct VOutBase {
    glm::vec4 position;

private:
    uint32_t drawcallID = 0;

    friend class Renderer;
};

struct PipelineState
{
    enum class CullMode { None, Front, Back };

    bool depthTest;
    bool depthWrite;
	CullMode cullMode = CullMode::Back;
};

template<typename P, typename Uniform, typename VInput, typename VOutputStruct>
concept Pipeline = std::is_empty_v<P> &&
	std::derived_from<VOutputStruct, VOutBase> && 
    requires (const VInput* v_in, const Uniform* uni, const VOutputStruct* in_v, glm::vec3 weights) 
{
    { P::vertexShader(v_in, uni) } -> std::same_as<VOutputStruct>;
    { P::interpolationShader(in_v, in_v, in_v, weights, uni) } -> std::same_as<VOutputStruct>;
    { P::fragmentShader(in_v, uni) } -> std::same_as<glm::vec4>;
};

template<typename P>
concept HasBlendShader = requires (const glm::vec4 * col) {
    { P::blendShader(col, col) } -> std::same_as<glm::vec4>;
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
}