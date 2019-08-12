// Copyright (c) Peter Hillerström (skipifzero.com, peter@hstroem.se)
//               For other contributors see Contributors.txt
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "ph/renderer/RendererUI.hpp"

#include <utility> // std::swap()

#include <imgui.h>

#include "ph/Context.hpp"
#include "ph/renderer/NextGenRendererState.hpp"
#include "ph/renderer/ZeroGUtils.hpp"

namespace ph {

// Statics
// ------------------------------------------------------------------------------------------------

template<typename Fun>
static void alignedEdit(const char* name, float xOffset, Fun editor) noexcept
{
	ImGui::Text("%s:", name);
	ImGui::SameLine(xOffset);
	editor(sfz::str96("##%s_invisible", name).str);
}

static const char* toString(StageType type) noexcept
{
	switch (type) {
	case StageType::USER_INPUT_RENDERING: return "USER_INPUT_RENDERING";
	case StageType::USER_STAGE_BARRIER: return "USER_STAGE_BARRIER";
	}
	sfz_assert_debug(false);
	return "<ERROR>";
}

static const char* textureFormatToString(ZgTexture2DFormat format) noexcept
{
	switch (format) {
	case ZG_TEXTURE_2D_FORMAT_UNDEFINED: return "UNDEFINED";

	case ZG_TEXTURE_2D_FORMAT_R_U8: return "R_U8";
	case ZG_TEXTURE_2D_FORMAT_RG_U8: return "RG_U8";
	case ZG_TEXTURE_2D_FORMAT_RGBA_U8: return "RGBA_U8";
	}
	sfz_assert_debug(false);
	return "";
}

static const char* vertexAttributeTypeToString(ZgVertexAttributeType type) noexcept
{
	switch (type) {
	case ZG_VERTEX_ATTRIBUTE_F32: return "ZG_VERTEX_ATTRIBUTE_F32";
	case ZG_VERTEX_ATTRIBUTE_F32_2: return "ZG_VERTEX_ATTRIBUTE_F32_2";
	case ZG_VERTEX_ATTRIBUTE_F32_3: return "ZG_VERTEX_ATTRIBUTE_F32_3";
	case ZG_VERTEX_ATTRIBUTE_F32_4: return "ZG_VERTEX_ATTRIBUTE_F32_4";

	case ZG_VERTEX_ATTRIBUTE_S32: return "ZG_VERTEX_ATTRIBUTE_S32";
	case ZG_VERTEX_ATTRIBUTE_S32_2: return "ZG_VERTEX_ATTRIBUTE_S32_2";
	case ZG_VERTEX_ATTRIBUTE_S32_3: return "ZG_VERTEX_ATTRIBUTE_S32_3";
	case ZG_VERTEX_ATTRIBUTE_S32_4: return "ZG_VERTEX_ATTRIBUTE_S32_4";

	case ZG_VERTEX_ATTRIBUTE_U32: return "ZG_VERTEX_ATTRIBUTE_U32";
	case ZG_VERTEX_ATTRIBUTE_U32_2: return "ZG_VERTEX_ATTRIBUTE_U32_2";
	case ZG_VERTEX_ATTRIBUTE_U32_3: return "ZG_VERTEX_ATTRIBUTE_U32_3";
	case ZG_VERTEX_ATTRIBUTE_U32_4: return "ZG_VERTEX_ATTRIBUTE_U32_4";

	default: break;
	}
	sfz_assert_debug(false);
	return "";
}

static const char* samplingModeToString(ZgSamplingMode mode) noexcept
{
	switch (mode) {
	case ZG_SAMPLING_MODE_NEAREST: return "NEAREST";
	case ZG_SAMPLING_MODE_TRILINEAR: return "TRILINEAR";
	case ZG_SAMPLING_MODE_ANISOTROPIC: return "ANISOTROPIC";
	}
	sfz_assert_debug(false);
	return "UNDEFINED";
}

static const char* wrappingModeToString(ZgWrappingMode mode) noexcept
{
	switch (mode) {
	case ZG_WRAPPING_MODE_CLAMP: return "CLAMP";
	case ZG_WRAPPING_MODE_REPEAT: return "REPEAT";
	}
	sfz_assert_debug(false);
	return "UNDEFINED";
}

static const char* depthFuncToString(ZgDepthFunc func) noexcept
{
	switch (func) {
	case ZG_DEPTH_FUNC_LESS: return "LESS";
	case ZG_DEPTH_FUNC_LESS_EQUAL: return "LESS_EQUAL";
	case ZG_DEPTH_FUNC_EQUAL: return "EQUAL";
	case ZG_DEPTH_FUNC_NOT_EQUAL: return "NOT_EQUAL";
	case ZG_DEPTH_FUNC_GREATER: return "GREATER";
	case ZG_DEPTH_FUNC_GREATER_EQUAL: return "GREATER_EQUAL";
	}
	sfz_assert_debug(false);
	return "";
}

// RendererUI: State methods
// ------------------------------------------------------------------------------------------------

void RendererUI::swap(RendererUI& other) noexcept
{

}

void RendererUI::destroy() noexcept
{

}

// RendererUI: Methods
// ------------------------------------------------------------------------------------------------

void RendererUI::render(NextGenRendererState& state) noexcept
{
	ImGuiWindowFlags windowFlags = 0;
	windowFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
	if (!ImGui::Begin("Renderer", nullptr, windowFlags)) {
		ImGui::End();
		return;
	}

	// Tabs
	ImGuiTabBarFlags tabBarFlags = ImGuiTabBarFlags_None;
	if (ImGui::BeginTabBar("RendererTabBar", tabBarFlags)) {
		
		if (ImGui::BeginTabItem("General")) {
			ImGui::Spacing();
			this->renderGeneralTab(state);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Stages")) {
			ImGui::Spacing();
			this->renderStagesTab(state.configurable);
			ImGui::EndTabItem();
		}
		
		if (ImGui::BeginTabItem("Pipelines")) {
			ImGui::Spacing();
			this->renderPipelinesTab(state.configurable);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Memory")) {
			ImGui::Spacing();
			this->renderMemoryTab(state);
			ImGui::EndTabItem();
		}
		
		if (ImGui::BeginTabItem("Textures")) {
			ImGui::Spacing();
			this->renderTexturesTab(state);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Meshes")) {
			ImGui::Spacing();
			this->renderMeshesTab(state);
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

// RendererUI: Private methods
// --------------------------------------------------------------------------------------------

void RendererUI::renderGeneralTab(NextGenRendererState& state) noexcept
{
	constexpr float offset = 250.0f;
	alignedEdit("Current frame index", offset, [&](const char*) {
		ImGui::Text("%llu", state.currentFrameIdx);
	});
	alignedEdit("Window resolution", offset, [&](const char*) {
		ImGui::Text("%i x %i", state.windowRes.x, state.windowRes.y);
	});
}

void RendererUI::renderStagesTab(RendererConfigurableState& state) noexcept
{
	// Get global collection of resource strings in order to get strings from StringIDs
	sfz::StringCollection& resStrings = ph::getResourceStrings();

	for (uint32_t i = 0; i < state.presentQueueStages.size(); i++) {
		const Stage& stage = state.presentQueueStages[i];

		// Stage name
		ImGui::Text("Stage %u - \"%s\"", i, resStrings.getString(stage.stageName));
		ImGui::Indent(20.0f);

		// Stage type
		ImGui::Text("Type: %s", toString(stage.stageType));

		if (stage.stageType != StageType::USER_STAGE_BARRIER) {

			// Pipeline name
			ImGui::Text("Rendering Pipeline: \"%s\"", resStrings.getString(stage.renderingPipelineName));
		}

		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}

}

void RendererUI::renderPipelinesTab(RendererConfigurableState& state) noexcept
{
	// Get global collection of resource strings in order to get strings from StringIDs
	sfz::StringCollection& resStrings = ph::getResourceStrings();

	// Rendering pipelines
	ImGui::Text("Rendering Pipelines");
	ImGui::Spacing();
	for (uint32_t i = 0; i < state.renderingPipelines.size(); i++) {
		const PipelineRenderingItem& pipeline = state.renderingPipelines[i];
		const ZgPipelineRenderingSignature& signature = pipeline.pipeline.signature;

		// Pipeline name
		const char* name = resStrings.getString(pipeline.name);
		bool collapsingHeaderOpen =
			ImGui::CollapsingHeader(str256("Pipeline %u - \"%s\"", i, name).str);
		if (collapsingHeaderOpen) continue;
		ImGui::Indent(20.0f);

		// Valid or not
		ImGui::Indent(20.0f);
		if (!pipeline.pipeline.valid()) {
			ImGui::SameLine();
			ImGui::TextUnformatted("-- INVALID PIPELINE");
		}

		// Pipeline info
		ImGui::Spacing();
		ImGui::Text("Vertex Shader: \"%s\" -- \"%s\"",
			pipeline.vertexShaderPath.str, pipeline.vertexShaderEntry.str);
		ImGui::Text("Pixel Shader: \"%s\" -- \"%s\"",
			pipeline.pixelShaderPath.str, pipeline.pixelShaderEntry.str);

		// Print vertex attributes
		ImGui::Spacing();
		ImGui::Text("Vertex attributes (%u):", signature.numVertexAttributes);
		ImGui::Indent(20.0f);
		for (uint32_t j = 0; j < signature.numVertexAttributes; j++) {
			const ZgVertexAttribute& attrib = signature.vertexAttributes[j];
			ImGui::Text("- Location: %u -- Type: %s",
				attrib.location, vertexAttributeTypeToString(attrib.type));
		}
		ImGui::Unindent(20.0f);

		// Print constant buffers
		if (signature.numConstantBuffers > 0) {
			ImGui::Spacing();
			ImGui::Text("Constant buffers (%u):", signature.numConstantBuffers);
			ImGui::Indent(20.0f);
			for (uint32_t i = 0; i < signature.numConstantBuffers; i++) {
				const ZgConstantBufferDesc& cbuffer = signature.constantBuffers[i];
				ImGui::Text("- Register: %u -- Size: %u bytes -- Push constant: %s",
					cbuffer.shaderRegister,
					cbuffer.sizeInBytes,
					cbuffer.pushConstant == ZG_TRUE ? "YES" : "NO");
			}
			ImGui::Unindent(20.0f);
		}

		// Print textures
		if (signature.numTextures > 0) {
			ImGui::Spacing();
			ImGui::Text("Textures (%u):", signature.numTextures);
			ImGui::Indent(20.0f);
			for (uint32_t j = 0; j < signature.numTextures; j++) {
				const ZgTextureDesc& texture = signature.textures[j];
				ImGui::Text("- Register: %u", texture.textureRegister);
			}
			ImGui::Unindent(20.0f);
		}

		// Print samplers
		if (pipeline.numSamplers > 0) {
			ImGui::Spacing();
			ImGui::Text("Samplers (%u):", pipeline.numSamplers);
			ImGui::Indent(20.0f);
			for (uint32_t j = 0; j < pipeline.numSamplers; j++) {
				const SamplerItem& item = pipeline.samplers[j];
				ImGui::Text("- Register: %u -- Sampling: %s -- Wrapping: %s",
					item.samplerRegister,
					samplingModeToString(item.sampler.samplingMode),
					wrappingModeToString(item.sampler.wrappingModeU));

			}
			ImGui::Unindent(20.0f);
		}

		// Print depth test
		ImGui::Spacing();
		ImGui::Text("Depth Test: %s", pipeline.depthTest ? "ENABLED" : "DISABLED");
		if (pipeline.depthTest) {
			ImGui::Indent(20.0f);
			ImGui::Text("Depth function: %s", depthFuncToString(pipeline.depthFunc));
			ImGui::Unindent(20.0f);
		}

		ImGui::Unindent(20.0f);
		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Text("Compute Pipelines");
}

void RendererUI::renderMemoryTab(NextGenRendererState& state) noexcept
{
	// Get ZeroG stats
	ZgStats stats = {};
	CHECK_ZG state.zgCtx.getStats(stats);

	// Lambdas for converting bytes to various units
	auto toGiB = [](uint64_t bytes) {
		return float(bytes) / (1024.0f * 1024.0f * 1024.0f);
	};
	auto toMiB = [](uint64_t bytes) {
		return float(bytes) / (1024.0f * 1024.0f);
	};

	// Print ZeroG statistics
	ImGui::Text("ZeroG Statistics");
	ImGui::Spacing();
	ImGui::Indent(20.0f);

	constexpr float statsValueOffset = 240.0f;
	alignedEdit("Device Description", statsValueOffset, [&](const char*) {
		ImGui::TextUnformatted(stats.deviceDescription);
	});
	ImGui::Spacing();
	alignedEdit("Dedicated GPU Memory", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.dedicatedGpuMemoryBytes));
	});
	alignedEdit("Dedicated CPU Memory", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.dedicatedCpuMemoryBytes));
	});
	alignedEdit("Shared GPU Memory", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.sharedCpuMemoryBytes));
	});
	ImGui::Spacing();
	alignedEdit("Memory Budget", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.memoryBudgetBytes));
	});
	alignedEdit("Current Memory Usage", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.memoryUsageBytes));
	});
	ImGui::Spacing();
	alignedEdit("Non-Local Budget", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.nonLocalBugetBytes));
	});
	alignedEdit("Non-Local Usage", statsValueOffset, [&](const char*) {
		ImGui::Text("%.2f GiB", toGiB(stats.nonLocalUsageBytes));
	});

	ImGui::Unindent(20.0f);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	DynamicGpuAllocator& alloc = state.dynamicAllocator;
	ImGui::Text("Dynamic Memory Allocation");
	ImGui::Spacing();
	ImGui::Indent(10.0f);


	ImGui::Text("Device Memory");
	ImGui::Indent(30.0f);
	ImGui::Spacing();
	constexpr float infoOffset = 280.0f;
	alignedEdit("Total Num Allocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumAllocationsDevice());
	});
	alignedEdit("Total Num Deallocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumDeallocationsDevice());
	});
	alignedEdit("Default Page Size", infoOffset, [&](const char*) {
		ImGui::Text("%.2f MiB", toMiB(alloc.queryDefaultPageSizeDevice()));
	});
	uint32_t numDevicePages = alloc.queryNumPagesDevice();
	alignedEdit("Num Pages", infoOffset, [&](const char*) {
		ImGui::Text("%u", numDevicePages);
	});
	ImGui::Spacing();
	for (uint32_t i = 0; i < numDevicePages; i++) {
		constexpr float pageOffset = 260.0f;
		PageInfo info = alloc.queryPageInfoDevice(i);
		ImGui::Text("Page %u:", i);
		ImGui::Indent(20.0f);
		alignedEdit("Size", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.pageSizeBytes));
		});
		alignedEdit("Num Allocations", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numAllocations);
		});
		alignedEdit("Num Free Blocks", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numFreeBlocks);
		});
		alignedEdit("Largest Free Block", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.largestFreeBlockBytes));
		});
		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}
	ImGui::Unindent(30.0f);


	ImGui::Spacing();
	ImGui::Text("Upload Memory");
	ImGui::Indent(30.0f);
	ImGui::Spacing();
	alignedEdit("Total Num Allocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumAllocationsUpload());
	});
	alignedEdit("Total Num Deallocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumDeallocationsUpload());
	});
	alignedEdit("Default Page Size", infoOffset, [&](const char*) {
		ImGui::Text("%.2f MiB", toMiB(alloc.queryDefaultPageSizeUpload()));
	});
	uint32_t numUploadPages = alloc.queryNumPagesUpload();
	alignedEdit("Num Pages", infoOffset, [&](const char*) {
		ImGui::Text("%u", numUploadPages);
	});
	ImGui::Spacing();
	for (uint32_t i = 0; i < numUploadPages; i++) {
		constexpr float pageOffset = 260.0f;
		PageInfo info = alloc.queryPageInfoUpload(i);
		ImGui::Text("Page %u:", i);
		ImGui::Indent(20.0f);
		alignedEdit("Size", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.pageSizeBytes));
		});
		alignedEdit("Num Allocations", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numAllocations);
		});
		alignedEdit("Num Free Blocks", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numFreeBlocks);
		});
		alignedEdit("Largest Free Block", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.largestFreeBlockBytes));
		});
		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}
	ImGui::Unindent(30.0f);


	ImGui::Spacing();
	ImGui::Text("Texture Memory");
	ImGui::Indent(30.0f);
	ImGui::Spacing();
	alignedEdit("Total Num Allocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumAllocationsTexture());
	});
	alignedEdit("Total Num Deallocations", infoOffset, [&](const char*) {
		ImGui::Text("%u", alloc.queryTotalNumDeallocationsTexture());
	});
	alignedEdit("Default Page Size", infoOffset, [&](const char*) {
		ImGui::Text("%.2f MiB", toMiB(alloc.queryDefaultPageSizeTexture()));
	});
	uint32_t numTexturePages = alloc.queryNumPagesTexture();
	alignedEdit("Num Pages", infoOffset, [&](const char*) {
		ImGui::Text("%u", numTexturePages);
	});
	ImGui::Spacing();
	for (uint32_t i = 0; i < numTexturePages; i++) {
		constexpr float pageOffset = 260.0f;
		PageInfo info = alloc.queryPageInfoTexture(i);
		ImGui::Text("Page %u:", i);
		ImGui::Indent(20.0f);
		alignedEdit("Size", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.pageSizeBytes));
		});
		alignedEdit("Num Allocations", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numAllocations);
		});
		alignedEdit("Num Free Blocks", pageOffset, [&](const char*) {
			ImGui::Text("%u", info.numFreeBlocks);
		});
		alignedEdit("Largest Free Block", pageOffset, [&](const char*) {
			ImGui::Text("%.2f MiB", toMiB(info.largestFreeBlockBytes));
		});
		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}
	ImGui::Unindent(30.0f);


	ImGui::Unindent(10.0f);
}

void RendererUI::renderTexturesTab(NextGenRendererState& state) noexcept
{
	// Get global collection of resource strings in order to get strings from StringIDs
	sfz::StringCollection& resStrings = ph::getResourceStrings();

	constexpr float offset = 150.0f;

	for (auto itemItr : state.textures) {
		const TextureItem& item = itemItr.value;

		ImGui::Text("\"%s\"", resStrings.getString(itemItr.key));
		if (!item.texture.valid()) {
			ImGui::SameLine();
			ImGui::Text("-- NOT VALID");
		}

		ImGui::Indent(20.0f);
		alignedEdit("Format", offset, [&](const char*) {
			ImGui::Text("%s", textureFormatToString(item.format));
		});
		alignedEdit("Resolution", offset, [&](const char*) {
			ImGui::Text("%u x %u", item.width, item.height);
		});
		alignedEdit("Mipmaps", offset, [&](const char*) {
			ImGui::Text("%u", item.numMipmaps);
		});
		

		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}
}

void RendererUI::renderMeshesTab(NextGenRendererState& state) noexcept
{
	// Get global collection of resource strings in order to get strings from StringIDs
	sfz::StringCollection& resStrings = ph::getResourceStrings();

	for (auto itemItr : state.meshes) {
		const GpuMesh& mesh = itemItr.value;

		// Check if mesh is valid
		bool meshValid = true;
		if (!mesh.vertexBuffer.valid()) meshValid = false;
		if (!mesh.materialsBuffer.valid()) meshValid = false;
		for (const GpuMeshComponent& comp : mesh.components) {
			if (!comp.indexBuffer.valid()) meshValid = false;
		}
		
		// Mesh name
		ImGui::Text("\"%s\"", resStrings.getString(itemItr.key));
		if (!meshValid) {
			ImGui::SameLine();
			ImGui::Text("-- NOT VALID");
		}

		ImGui::Indent(20.0f);
		for (uint32_t i = 0; i < mesh.components.size(); i++) {
			const GpuMeshComponent& comp = mesh.components[i];

			constexpr float offset = 250.0f;
			ImGui::Text("Component %u:", i);
			ImGui::Indent(20.0f);
			alignedEdit("- Material Index", offset, [&](const char*) {
				ImGui::Text("%u", comp.cpuMaterial.materialIdx);
			});
			auto printTextureId = [&](const char* name, StringID texID) {
				if (texID == StringID::invalid()) return;
				alignedEdit(name, offset, [&](const char*) {
					ImGui::Text("%s", resStrings.getString(texID));
				});
			};
			printTextureId("- Albedo Texture", comp.cpuMaterial.albedoTex);
			printTextureId("- Metallic Roughness Texture", comp.cpuMaterial.metallicRoughnessTex);
			printTextureId("- Normal Texture", comp.cpuMaterial.normalTex);
			printTextureId("- Occlusion Texture", comp.cpuMaterial.occlusionTex);
			printTextureId("- Emissive Texture", comp.cpuMaterial.emissiveTex);

			ImGui::Unindent(20.0f);
			ImGui::Spacing();
		}

		ImGui::Unindent(20.0f);
		ImGui::Spacing();
	}
}

} // namespace ph
