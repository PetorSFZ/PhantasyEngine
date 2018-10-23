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

#pragma once

#include <sfz/containers/DynArray.hpp>
#include <sfz/memory/Allocator.hpp>
#include <sfz/memory/SmartPointers.hpp>

#include "ph/game_loop/GameLoopUpdateable.hpp"
#include "ph/rendering/CameraData.hpp"
#include "ph/rendering/Image.hpp"
#include "ph/rendering/LevelAssets.hpp"
#include "ph/rendering/Mesh.hpp"

namespace ph {

using sfz::Allocator;
using sfz::DynArray;
using sfz::UniquePtr;

// DefaultGameUpdateable logic
// ------------------------------------------------------------------------------------------------

struct UpdateableState final {
	phCameraData cam;
	LevelAssets dynamicAssets;
	
	DynArray<phRenderEntity> renderEntities;
	DynArray<phSphereLight> dynamicSphereLights;
};

struct ImguiControllers final {
	bool useMouse = true;
	bool useKeyboard = true;
	int32_t controllerIndex = -1;
};

class GameLogic {
public:
	virtual ~GameLogic() {}

	virtual void initialize(UpdateableState& state, Renderer& renderer) = 0;

	// Returns the index of the controller to be used for Imgui. If -1 is returned no controller
	// input will be provided to Imgui.
	virtual ImguiControllers imguiController(const UserInput&) { return ImguiControllers(); }

	virtual UpdateOp processInput(
		UpdateableState& state,
		const UserInput& input,
		const UpdateInfo& updateInfo,
		Renderer& renderer) = 0;

	virtual UpdateOp updateTick(UpdateableState& state, const UpdateInfo& updateInfo) = 0;

	// Renders custom Imgui commands.
	//
	// This is the only function where Imgui commands can safely be called. BeginFrame() and
	// EndFrame() are called before and after this function. Other Imgui commands from the
	// DefaultGameUpdateable console itself may be sent within this same frame if they are set to
	// be always shown. This function will not be called if the console is currently active.
	virtual void renderCustomImgui() {}

	// Called when console is activated. The logic instance will not receive any additional calls
	// until the console is closed, at which point onConsoleDeactivated() will be called. onQuit()
	// may be called before the console is deactivated.
	virtual void onConsoleActivated() {}

	// Called when the console is deactivated.
	virtual void onConsoleDeactivated() {}

	virtual void onQuit(UpdateableState& state) { (void)state; }
};

// DefaultGameUpdateable creation function
// ------------------------------------------------------------------------------------------------

UniquePtr<GameLoopUpdateable> createDefaultGameUpdateable(
	Allocator* allocator,
	UniquePtr<GameLogic> logic) noexcept;

} // namespace ph