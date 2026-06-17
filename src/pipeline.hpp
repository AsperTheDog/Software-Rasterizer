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
        outputFormatSize = sizeof(typename T::value_type);
        outputFormatNorm = std::is_floating_point_v<typename T::value_type>;
    }
};

struct ComputeContext
{
    glm::uvec3 numWorkGroups;
    glm::uvec3 globalInvocationID;
    glm::uvec3 localInvocationID;
    glm::uvec3 workGroupID;
};

template<typename P>
concept Pipeline = std::is_empty_v<P> && requires 
{
    typename P::Uniform;
    typename P::VInput;
    // VOutput must only contain floats
    typename P::VOutput;
} 
	&& std::derived_from<typename P::VOutput, VOutBase> 
	&& requires (const typename P::VInput* v_in, const typename P::Uniform* uni, const typename P::VOutput* in_v, const float tpw) 
{
    { P::vertexShader(v_in, uni) } -> std::same_as<typename P::VOutput>;
	// tpw only needed for mipmapped texture sampling, can be ignored otherwise
    { P::fragmentShader(in_v, uni, tpw) } -> std::same_as<glm::vec4>;
};

template<typename P>
concept HasBlendShader = Pipeline<P> && requires (const glm::vec4& col, const typename P::Uniform * uni) {
    { P::blendShader(col, col, uni) } -> std::same_as<glm::vec4>;
};

template<typename P>
concept HasAccurateMip = Pipeline<P> && requires (const typename P::VOutput * in_v) {
	{ P::getUV(in_v) } -> std::same_as<glm::vec2>;
};

template<typename P>
concept ComputePipeline = requires
{
    typename P::Uniform;
}
	&& requires (const ComputeContext& ctx, const typename P::Uniform* uni) 
{
    { P::computeShader(ctx, uni) } -> std::same_as<void>;
};