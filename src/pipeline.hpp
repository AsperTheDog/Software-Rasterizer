#pragma once
#include <glm.hpp>
#include <concepts>
#include <type_traits>

#include "shader_utils.hpp"
#include "texture.hpp"

struct PipelineState
{
    enum class CullMode : uint8_t { None, Front, Back };
    enum class DepthOp : uint8_t { Never, Less, Equal, Greater, NotEqual };

    bool depthTest;
    bool depthWrite;
	DepthOp depthOp = DepthOp::Less;

	CullMode cullMode = CullMode::Back;

    uint32_t outputFormatSize;
    uint32_t outputFormatNorm;

    template<PixelFormat T>
    void setFormat()
    {
        outputFormatSize = sizeof(T::value_type);
        outputFormatNorm = std::is_floating_point_v<typename T::value_type>;
    }
};

template<typename P>
concept Pipeline = std::is_empty_v<P> && requires 
{
    typename P::Uniform;
    typename P::VInput;
    typename P::VOutput;
} 
	&& std::derived_from<typename P::VOutput, VOutBase> 
	&& requires (const typename P::VInput* v_in, const typename P::Uniform* uni, const typename P::VOutput* in_v, glm::vec3 weights) 
{
    { P::vertexShader(v_in, uni) } -> std::same_as<typename P::VOutput>;
    { P::interpolationShader(in_v, in_v, in_v, weights, uni) } -> std::same_as<typename P::VOutput>;
    { P::fragmentShader(in_v, uni) } -> std::same_as<glm::vec4>;
};

template<typename P>
concept HasBlendShader = Pipeline<P> && requires (const glm::vec4& col, const typename P::Uniform * uni) {
    { P::blendShader(col, col, uni) } -> std::same_as<glm::vec4>;
};