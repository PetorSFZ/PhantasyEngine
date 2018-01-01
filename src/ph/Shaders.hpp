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

namespace ph {

// Shaders constants
// ------------------------------------------------------------------------------------------------

const uint32_t MAX_NUM_DYNAMIC_SPHERE_LIGHTS = 32;

// Header
// ------------------------------------------------------------------------------------------------

extern const char* SHADER_HEADER_SRC;

// Forward shading
// ------------------------------------------------------------------------------------------------

extern const char* VERTEX_SHADER_SRC;

extern const char* FRAGMENT_SHADER_SRC;

// Copy out shader
// ------------------------------------------------------------------------------------------------

extern const char* COPY_OUT_SHADER_SRC;

} // namespace ph
