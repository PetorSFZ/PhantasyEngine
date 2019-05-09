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

#include <cstdint>

#include "ph/rendering/Material.hpp"
#include "ph/rendering/MeshView.hpp"
#include "ph/rendering/RenderEntity.hpp"
#include "ph/rendering/SphereLight.hpp"

struct phStaticSceneView {
	const phMaterial* materials = nullptr;
	uint32_t numMaterials = 0;
	const phRenderEntity* renderEntities = nullptr;
	uint32_t numRenderEntities = 0;
	const phSphereLight* sphereLights = nullptr;
	uint32_t numSphereLights = 0;
};
