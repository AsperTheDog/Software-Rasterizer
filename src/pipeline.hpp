#pragma once
#include <glm.hpp>
#include <concepts>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

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

inline constexpr uint32_t kMaxColorTargets = 4;

struct BasicColorOut
{
	glm::vec4 color;
};

template<PixelFormat... T>
struct TargetList
{
	static constexpr uint32_t count = sizeof...(T);

	template<uint32_t I>
	using At = std::tuple_element_t<I, std::tuple<T...>>;
};

template<typename L>
inline constexpr bool kIsTargetList = false;

template<PixelFormat... T>
inline constexpr bool kIsTargetList<TargetList<T...>> = true;

template<typename R>
inline constexpr bool kIsOptional = false;

template<typename R>
inline constexpr bool kIsOptional<std::optional<R>> = true;

template<typename R>
struct UnwrapOptional { using type = R; };

template<typename R>
struct UnwrapOptional<std::optional<R>> { using type = R; };

struct Vec4Probe
{
	operator glm::vec4() const;
};

template<typename R, typename Seq>
inline constexpr bool kVec4Members = false;

template<typename R, size_t... I>
inline constexpr bool kVec4Members<R, std::index_sequence<I...>> = requires { R{ ((void)I, Vec4Probe{})... }; };

template<typename R>
concept FragmentOutput = std::is_aggregate_v<R>
	&& std::is_standard_layout_v<R>
	&& std::is_trivially_copyable_v<R>
	&& sizeof(R) % sizeof(glm::vec4) == 0
	&& sizeof(R) / sizeof(glm::vec4) >= 1
	&& sizeof(R) / sizeof(glm::vec4) <= kMaxColorTargets
	&& kVec4Members<R, std::make_index_sequence<sizeof(R) / sizeof(glm::vec4)>>;

template<typename R>
concept FragmentResult = FragmentOutput<typename UnwrapOptional<R>::type>;

template<typename P>
concept HasInstanceInput = requires { typename P::InstanceInput; };

struct NoInstanceInput {};

template<typename P>
struct InstanceInputOf { using type = NoInstanceInput; };

template<HasInstanceInput P>
struct InstanceInputOf<P> { using type = typename P::InstanceInput; };

template<typename P>
concept PerVertexFragmentShader = !HasInstanceInput<P>
	&& requires (const typename P::VOutput* in_v, const typename P::Uniform* uni, const float tpw)
{
	{ P::fragmentShader(in_v, uni, tpw) } -> FragmentResult;
};

template<typename P>
concept InstancedFragmentShader = HasInstanceInput<P>
	&& requires (const typename P::VOutput* in_v, const typename P::InstanceInput* inst, const typename P::Uniform* uni, const float tpw)
{
	{ P::fragmentShader(in_v, inst, uni, tpw) } -> FragmentResult;
};

template<typename P>
struct FragmentReturnOf {};

template<PerVertexFragmentShader P>
struct FragmentReturnOf<P>
{
	using type = decltype(P::fragmentShader(std::declval<const typename P::VOutput*>(), std::declval<const typename P::Uniform*>(), 0.0f));
};

template<InstancedFragmentShader P>
struct FragmentReturnOf<P>
{
	using type = decltype(P::fragmentShader(std::declval<const typename P::VOutput*>(), std::declval<const typename P::InstanceInput*>(), std::declval<const typename P::Uniform*>(), 0.0f));
};

template<typename P>
using FragmentReturn = FragmentReturnOf<P>::type;

template<typename P>
using FragmentOutputOf = UnwrapOptional<FragmentReturn<P>>::type;

template<typename P>
concept PerVertexShader = !HasInstanceInput<P>
	&& requires (const typename P::VInput* v_in, const typename P::Uniform* uni)
{
	{ P::vertexShader(v_in, uni) } -> std::same_as<typename P::VOutput>;
};

template<typename P>
concept InstancedVertexShader = HasInstanceInput<P>
	&& requires (const typename P::VInput* v_in, const typename P::InstanceInput* inst, const typename P::Uniform* uni)
{
	{ P::vertexShader(v_in, inst, uni) } -> std::same_as<typename P::VOutput>;
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
	&& (PerVertexShader<P> || InstancedVertexShader<P>);

template<typename P>
concept InstancedPipeline = Pipeline<P> && HasInstanceInput<P>;

template<typename P>
concept HasFragmentShader = Pipeline<P>
	// tpw only needed for mipmapped texture sampling, can be ignored otherwise
	&& (PerVertexFragmentShader<P> || InstancedFragmentShader<P>)
	&& requires { typename P::Targets; }
	&& kIsTargetList<typename P::Targets>
	&& P::Targets::count <= kMaxColorTargets
	&& P::Targets::count == sizeof(FragmentOutputOf<P>) / sizeof(glm::vec4);

template<typename P>
concept DepthOnlyPipeline = Pipeline<P> && !requires { &P::fragmentShader; };

template<typename P>
concept HasBlendShader = Pipeline<P> && requires (const glm::vec4& col, const typename P::Uniform * uni) 
{
    { P::blendShader(col, col, uni) } -> std::same_as<glm::vec4>;
};

template<typename P>
concept HasAccurateMip = Pipeline<P> && requires (const typename P::VOutput * in_v) 
{
	{ P::getUV(in_v) } -> std::same_as<glm::vec2>;
};

template<typename P>
concept HasDiscardingFragmentShader = HasFragmentShader<P> && kIsOptional<FragmentReturn<P>>;

template<typename P>
concept ComputePipeline = requires
{
    typename P::Uniform;
}
	&& requires (const ComputeContext& ctx, const typename P::Uniform* uni) 
{
    { P::computeShader(ctx, uni) } -> std::same_as<void>;
};