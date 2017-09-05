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

#include "ph/rendering/Renderer.hpp"

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef near
#undef far
#endif

#include <SDL.h>

#include <sfz/Assert.hpp>
#include <sfz/memory/New.hpp>
#include <sfz/strings/StackString.hpp>

#include "ph/config/GlobalConfig.hpp"
#include "ph/utils/Logging.hpp"

namespace ph {

using sfz::StackString;
using sfz::StackString192;
using std::uint32_t;

// Function Table struct
// ------------------------------------------------------------------------------------------------

extern "C" {
	struct FunctionTable {
		// Init functions
		uint32_t (*phRendererInterfaceVersion)(void);
		uint32_t (*phRequiredSDL2WindowFlags)(void);
		uint32_t (*phInitRenderer)(SDL_Window*, sfzAllocator*, phConfig*, phLogger*);
		uint32_t (*phDeinitRenderer)(void);

		// Render commands
		void (*phBeginFrame)(const phCameraData*, const phSphereLight*, uint32_t);
		void (*phRender)(const phRenderEntity*, uint32_t);
		void (*phFinishFrame)(void);
	};
}

// Statics
// ------------------------------------------------------------------------------------------------

#ifdef _WIN32
static StackString192 getWindowsErrorMessage() noexcept
{
	StackString192 str;
	DWORD errorCode = GetLastError();
	if (errorCode != 0) {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errorCode,
		    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str.str, sizeof(str.str), NULL);
	}
	return str;
}

#define LOAD_FUNCTION(module, table, functionName) \
	{ \
		using FunctionType = decltype(FunctionTable::functionName); \
		table->functionName = (FunctionType)GetProcAddress((HMODULE)module, #functionName); \
		if (table->functionName == nullptr) { \
			StackString192 error = getWindowsErrorMessage(); \
			PH_LOG(LOG_LEVEL_ERROR, "PhantasyEngine", "Failed to load %s(), message: %s", \
			    #functionName, error.str); \
		} \
	}
#endif

// Renderer: Constructors & destructors
// ------------------------------------------------------------------------------------------------

Renderer::Renderer(const char* moduleName, Allocator* allocator) noexcept
{
	this->load(moduleName, allocator);
}

Renderer::Renderer(Renderer&& other) noexcept
{
	this->swap(other);
}

Renderer& Renderer::operator= (Renderer&& other) noexcept
{
	this->swap(other);
	return *this;
}

Renderer::~Renderer() noexcept
{
	this->destroy();
}

// Renderer: Methods
// ------------------------------------------------------------------------------------------------

void Renderer::load(const char* moduleName, Allocator* allocator) noexcept
{
	sfz_assert_debug(moduleName != nullptr);
	sfz_assert_debug(allocator != nullptr);
	if (mModuleHandle != nullptr) this->destroy();

	// Load DLL on Windows
#ifdef _WIN32
	// Load DLL
	{
		StackString dllName;
		dllName.printf("%s.dll", moduleName);
		mModuleHandle = LoadLibrary(dllName.str);
	}
	if (mModuleHandle == nullptr) {
		StackString192 error = getWindowsErrorMessage();
		PH_LOG(LOG_LEVEL_ERROR, "PhantasyEngine", "Failed to load DLL (%s), message: %s",
		    moduleName, error.str);
		return;
	}
#endif

	// Set allocator
	mAllocator = allocator;

	// Create function table
	mFunctionTable = sfz::sfzNew<FunctionTable>(allocator);
	std::memset(mFunctionTable, 0, sizeof(FunctionTable));

	// Load functions from DLL on Windows
#ifdef _WIN32
	// Start of with loading interface version function and checking that the correct interface is used
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phRendererInterfaceVersion);
	if (INTERFACE_VERSION != mFunctionTable->phRendererInterfaceVersion()) {
		PH_LOG(LOG_LEVEL_ERROR, "PhantasyEngine", "Renderer DLL (%s.dll) has wrong interface version (%u), expected (%u).",
			moduleName, mFunctionTable->phRendererInterfaceVersion(), INTERFACE_VERSION);
	}

	// Init functions
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phRequiredSDL2WindowFlags);
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phInitRenderer);
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phDeinitRenderer);

	// Render commands
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phBeginFrame);
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phRender);
	LOAD_FUNCTION(mModuleHandle, mFunctionTable, phFinishFrame);
#endif

	
}

void Renderer::swap(Renderer& other) noexcept
{
	std::swap(this->mModuleHandle, other.mModuleHandle);
	std::swap(this->mAllocator, other.mAllocator);
	std::swap(this->mFunctionTable, other.mFunctionTable);
}

void Renderer::destroy() noexcept
{
	if (mModuleHandle != nullptr) {

		// Deinit renderer
		this->deinitRenderer();

		// Unload DLL on Windows
#ifdef _WIN32
		BOOL freeSuccess = FreeLibrary((HMODULE)mModuleHandle);
		if (!freeSuccess) {
			StackString192 error = getWindowsErrorMessage();
			PH_LOG(LOG_LEVEL_ERROR, "PhantasyEngine", "Failed to unload DLL, message: %s",
			    error.str);
		}
#endif
		
		// Deallocate function table
		sfz::sfzDelete(mFunctionTable, mAllocator);

		// Reset all variables
		mModuleHandle = nullptr;
		mAllocator = nullptr;
		mFunctionTable = nullptr;
		mInited = false;
	}
}

// Renderer: Renderer functions
// ------------------------------------------------------------------------------------------------

uint32_t Renderer::rendererInterfaceVersion() const noexcept
{
	sfz_assert_debug(mFunctionTable != nullptr);
	sfz_assert_debug(mFunctionTable->phRendererInterfaceVersion != nullptr);
	return mFunctionTable->phRendererInterfaceVersion();
}

uint32_t Renderer::requiredSDL2WindowFlags() const noexcept
{
	sfz_assert_debug(mFunctionTable != nullptr);
	sfz_assert_debug(mFunctionTable->phRequiredSDL2WindowFlags != nullptr);
	return mFunctionTable->phRequiredSDL2WindowFlags();
}

bool Renderer::initRenderer(SDL_Window* window) noexcept
{
	if (mInited) {
		PH_LOG(LOG_LEVEL_WARNING, "PhantasyEngine", "Trying to init renderer that is already inited");
		return true;
	}

	phConfig tmpConfig = GlobalConfig::cInstance();
	phLogger tmpLogger = getLogger();
	uint32_t initSuccess = mFunctionTable->phInitRenderer(window, mAllocator->cAllocator(), &tmpConfig, &tmpLogger);
	if (initSuccess == 0) {
		PH_LOG(LOG_LEVEL_ERROR, "PhantasyEngine", "Renderer failed to initialize.");
		return false;
	}

	mInited = true;
	return true;
}

void Renderer::deinitRenderer() noexcept
{
	sfz_assert_debug(mFunctionTable != nullptr);
	sfz_assert_debug(mFunctionTable->phDeinitRenderer != nullptr);
	if (mInited) {
		mFunctionTable->phDeinitRenderer();
	}
	mInited = false;
}

// Renderer: Render commands
// ------------------------------------------------------------------------------------------------

void Renderer::beginFrame(
	const phCameraData& camera,
	const phSphereLight* dynamicSphereLights,
	uint32_t numDynamicSphereLights) noexcept
{
	mFunctionTable->phBeginFrame(&camera, dynamicSphereLights, numDynamicSphereLights);
}

void Renderer::render(const phRenderEntity* entities, uint32_t numEntities) noexcept
{
	mFunctionTable->phRender(entities, numEntities);
}

void Renderer::finishFrame() noexcept
{
	mFunctionTable->phFinishFrame();
}

} // namespace ph
