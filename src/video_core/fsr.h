// SPDX-FileCopyrightText: Copyright 2025 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <numeric>
#include "common/common_types.h"
#include "common/math_util.h"
#include "common/settings_enums.h"

namespace FSR {
// Reimplementations of the constant generating functions in ffx_fsr1.h
// GCC generated a lot of warnings when using the official header.
void FsrEasuConOffset(u32 con0[4], u32 con1[4], u32 con2[4], u32 con3[4],
                      f32 inputViewportInPixelsX, f32 inputViewportInPixelsY,
                      f32 inputSizeInPixelsX, f32 inputSizeInPixelsY, f32 outputSizeInPixelsX,
                      f32 outputSizeInPixelsY, f32 inputOffsetInPixelsX, f32 inputOffsetInPixelsY);

void FsrRcasCon(u32* con, f32 sharpness);

/// UPGRADE [1]: Returns the output/input area ratio handled by FSR in this frame.
/// AMD designed EASU for area ratios between 1x and 4x; staying near 2.25x
/// (e.g. internal resolution 1.5x upscaled to the window) gives the best quality.
f32 AreaRatio(u32 input_width, u32 input_height, u32 output_width, u32 output_height);

/// UPGRADE [3]: Size in pixels of the valid image area inside a framebuffer of the given
/// size. Many Switch games use dynamic resolution, so only a sub-rectangle (the crop) of
/// the framebuffer holds the image. Area ratios must be judged on that sub-rectangle, not on
/// the whole texture. Falls back to the full size if the crop is degenerate.
struct PixelExtent {
    u32 width;
    u32 height;
};
PixelExtent CroppedExtent(u32 width, u32 height, const Common::Rectangle<f32>& crop);

/// UPGRADE [4]: Returns false when the FSR pass would be a downscale (area ratio < 1).
/// EASU is not a proper downscaling filter, so callers should skip FSR and let the
/// window adapt filter (bilinear/bicubic) perform the downscale instead.
bool IsUpscale(u32 input_width, u32 input_height, u32 output_width, u32 output_height);

/// UPGRADE [4]: Resolves the scaling filter that should actually run for this frame. FSR
/// (EASU) is not a downscaling filter, so a requested FSR with an area ratio < 1 is
/// replaced by Bilinear (the window adapt filter that FSR already presents through).
Settings::ScalingFilter ResolveScalingFilter(Settings::ScalingFilter requested, u32 input_width,
                                             u32 input_height, u32 output_width,
                                             u32 output_height);

/// UPGRADE [1]: Logs a warning (rate limited, once per log2 area-ratio bucket) when the
/// area ratio is outside AMD's recommended 1x-4x range, including a concrete
/// recommendation for the internal resolution setting.
void WarnAreaRatioOnce(u32 input_width, u32 input_height, u32 output_width, u32 output_height);

/// UPGRADE [7]: Maps the FSR sharpness slider (raw 0-200, displayed 0-100 %) to RCAS
/// sharpening stops, clamped to the 0-4 range accepted by FsrRcasCon.
f32 GetSharpeningStops();

/// UPGRADE [5]: Computes (and caches) the negative MIP LOD bias recommended by AMD
/// (-log2 of the per-axis upscale ratio, i.e. -0.5 * log2(area ratio), quantized to half
/// stops and clamped to [-2, 0]) while FSR is actively upscaling and the user opted in.
/// MIP selection works per axis, so the linear ratio is used rather than the area ratio.
/// Bumps LodBiasGeneration() whenever the cached value changes.
void UpdateNegativeLodBias(u32 input_width, u32 input_height, u32 output_width,
                           u32 output_height);

/// UPGRADE [5]: Resets the cached bias to 0. Must be called while a different scaling filter
/// is active, otherwise the bias from a previous FSR session would stick to every sampler.
void ClearNegativeLodBias();

/// UPGRADE [5]: Current cached negative LOD bias (0 when the feature is disabled or
/// FSR is not upscaling). Safe to call from any thread.
f32 CurrentNegativeLodBias();

/// UPGRADE [5]: Incremented every time CurrentNegativeLodBias() changes. Guest samplers are
/// cached with the bias baked in, so the texture cache compares this counter to know when
/// its samplers must be rebuilt. Safe to call from any thread.
u32 LodBiasGeneration();

} // namespace FSR
