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

#include "ph/rendering/ResourceManager.hpp"

#include <algorithm>

#include <sfz/Logging.hpp>

#include <ph/Context.hpp>

#include "ph/rendering/Image.hpp"

namespace ph {

// ResourceManager: Constructors & destructors
// ------------------------------------------------------------------------------------------------

ResourceManager ResourceManager::create(Renderer* renderer, Allocator* allocator) noexcept
{
	ResourceManager manager;
	manager.mAllocator = allocator;
	manager.mRenderer = renderer;

	// Ensure renderer has not associated textures already, will break things
	sfz_assert_debug(renderer->numTextures() == 0);

	return manager;
}

// ResourceManager: State methods
// ------------------------------------------------------------------------------------------------

void ResourceManager::swap(ResourceManager& other) noexcept
{
	std::swap(this->mAllocator, other.mAllocator);
	std::swap(this->mRenderer, other.mRenderer);
	this->mTextureMap.swap(other.mTextureMap);
}

void ResourceManager::destroy() noexcept
{
	mAllocator = nullptr;
	mRenderer = nullptr;
	mTextureMap.destroy();
}

// ResourceManager: Texture methods
// ------------------------------------------------------------------------------------------------

uint32_t ResourceManager::registerTexture(const char* globalPath) noexcept
{
	// Convert global path to StringID
	StringCollection& resourceStrings = getResourceStrings();
	const StringID globalPathId = resourceStrings.getStringID(globalPath);

	// Check if texture is available in renderer, return index if it is
	uint32_t* globalIdxPtr = mTextureMap.get(globalPathId);
	if (globalIdxPtr != nullptr) return *globalIdxPtr;

	// Create image from path
	ph::Image image = loadImage("", globalPath);
	if (image.rawData.data() == nullptr) {
		SFZ_ERROR("ResourceManager", "Could not load texture: \"%s\"", globalPath);
		return uint16_t(~0);
	}

	// Upload image to renderer
	phConstImageView imageView = image;
	uint16_t globalIdx = mRenderer->addTexture(imageView);

	// Record entry
	mTextures.add(ResourceMapping::create(globalPathId, globalIdx));
	mTextureMap.put(globalPathId, globalIdx);

	SFZ_INFO_NOISY("ResourceManager", "Loaded texture: \"%s\", global index -> %u",
		globalPath, globalIdx);

	return globalIdx;
}

bool ResourceManager::hasTexture(StringID globalPathId) const noexcept
{
	return mTextureMap.get(globalPathId) != nullptr;
}

const char* ResourceManager::debugTextureIndexToGlobalPath(uint32_t index) const noexcept
{
	const StringCollection& resourceStrings = getResourceStrings();
	for (const ResourceMapping& mapping : mTextures) {
		if (mapping.globalIdx == index) return resourceStrings.getString(mapping.globalPathId);
	}
	return "NO TEXTURE";
}

// ResourceManager: Mesh methods
// ------------------------------------------------------------------------------------------------

uint32_t ResourceManager::registerMesh(const char* globalPath, const Mesh& mesh) noexcept
{
	// Convert global path to StringID
	StringCollection& resourceStrings = getResourceStrings();
	const StringID globalPathId = resourceStrings.getStringID(globalPath);

	// Check if mesh is available in renderer, return index if it is
	uint32_t* globalIdxPtr = mMeshMap.get(globalPathId);
	if (globalIdxPtr != nullptr) return *globalIdxPtr;

	// Upload mesh to renderer
	phConstMeshView imageView = mesh;
	uint32_t globalIdx = mRenderer->addMesh(mesh);

	// Record entry
	mMeshes.add(ResourceMapping::create(globalPathId, globalIdx));
	mMeshMap.put(globalPathId, globalIdx);

	SFZ_INFO_NOISY("ResourceManager", "Loaded mesh: \"%s\", global index -> %u",
		globalPath, globalIdx);

	return globalIdx;
}

bool ResourceManager::hasMesh(StringID globalPathId) const noexcept
{
	return mMeshMap.get(globalPathId) != nullptr;
}

} // namespace ph