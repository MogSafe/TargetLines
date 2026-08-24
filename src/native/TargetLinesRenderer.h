#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d8.h>

#include <cstddef>

struct TargetLinesRenderer;

TargetLinesRenderer* targetlines_renderer_create(HMODULE module);
void targetlines_renderer_destroy(TargetLinesRenderer* renderer);
void targetlines_renderer_render(TargetLinesRenderer* renderer, IDirect3DDevice8* device);
bool targetlines_renderer_set_identity(TargetLinesRenderer* renderer, char const* identifier);
bool targetlines_renderer_replace_state(
    TargetLinesRenderer* renderer, char const* state, std::size_t state_size);
void targetlines_renderer_unbind_state(TargetLinesRenderer* renderer);
void targetlines_renderer_status(
    TargetLinesRenderer const* renderer, char* output, std::size_t output_size);
