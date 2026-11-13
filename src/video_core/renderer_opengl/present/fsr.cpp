// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>
#include <glad/glad.h>
#include "video_core/fsr.h"
#include "video_core/host_shaders/ffx_a_h.h"
#include "video_core/host_shaders/ffx_fsr1_h.h"
#include "video_core/host_shaders/full_screen_triangle_vert.h"
#include "video_core/host_shaders/opengl_fidelityfx_fsr_easu_frag.h"
#include "video_core/host_shaders/opengl_fidelityfx_fsr_frag.h"
#include "video_core/host_shaders/opengl_fidelityfx_fsr_rcas_frag.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/present/fsr.h"
#include "video_core/renderer_opengl/present/util.h"

namespace OpenGL {
using namespace FSR;

using FsrConstants = std::array<u32, 4 * 4>;

namespace {
// UPGRADE [6]: Inserts the FP16 define right after the #version line of a shader source.
// It must precede the #include of opengl_fidelityfx_fsr.frag so the A_HALF path
// (FsrEasuH/FsrRcasH) is selected at compile time.
void InsertDefineAfterVersion(std::string& shader_source, const std::string& defines) {
    const std::size_t version_pos = shader_source.find("#version");
    if (version_pos == std::string::npos) {
        return;
    }
    const std::size_t line_end = shader_source.find('\n', version_pos);
    if (line_end == std::string::npos) {
        return;
    }
    shader_source.insert(line_end + 1, defines);
}
} // Anonymous namespace

FSR::FSR(u32 output_width_, u32 output_height_) : width(output_width_), height(output_height_) {
    std::string fsr_source{HostShaders::OPENGL_FIDELITYFX_FSR_FRAG};
    ReplaceInclude(fsr_source, "ffx_a.h", HostShaders::FFX_A_H);
    ReplaceInclude(fsr_source, "ffx_fsr1.h", HostShaders::FFX_FSR1_H);

    std::string fsr_easu_source{HostShaders::OPENGL_FIDELITYFX_FSR_EASU_FRAG};
    std::string fsr_rcas_source{HostShaders::OPENGL_FIDELITYFX_FSR_RCAS_FRAG};
    ReplaceInclude(fsr_easu_source, "opengl_fidelityfx_fsr.frag", fsr_source);
    ReplaceInclude(fsr_rcas_source, "opengl_fidelityfx_fsr.frag", fsr_source);

    // UPGRADE [6]: use the FP16 (A_HALF) FSR path when the driver provides the
    // float16_t shader types, with an automatic fallback to the FP32 path otherwise.
    if (GLAD_GL_AMD_gpu_shader_half_float) {
        constexpr std::string_view fp16_define = "#define YUZU_USE_FP16 1\n";
        InsertDefineAfterVersion(fsr_easu_source, std::string{fp16_define});
        InsertDefineAfterVersion(fsr_rcas_source, std::string{fp16_define});
    }

    vert = CreateProgram(HostShaders::FULL_SCREEN_TRIANGLE_VERT, GL_VERTEX_SHADER);
    easu_frag = CreateProgram(fsr_easu_source, GL_FRAGMENT_SHADER);
    rcas_frag = CreateProgram(fsr_rcas_source, GL_FRAGMENT_SHADER);

    glProgramUniform2f(vert.handle, 0, 1.0f, -1.0f);
    glProgramUniform2f(vert.handle, 1, 0.0f, 1.0f);

    sampler = CreateBilinearSampler();
    framebuffer.Create();

    // UPGRADE [6]: AMD's header recommends 32bpp formats for the intermediate textures.
    // RGBA8 keeps a full 8-bit alpha (needed when layers are alpha blended afterwards),
    // halves the bandwidth of the 64bpp RGBA16F targets used before and, being UNORM,
    // also guarantees RCAS never sees values outside [0, 1].
    easu_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(easu_tex.handle, 1, GL_RGBA8, width, height);

    rcas_tex.Create(GL_TEXTURE_2D);
    glTextureStorage2D(rcas_tex.handle, 1, GL_RGBA8, width, height);
}

FSR::~FSR() = default;

GLuint FSR::Draw(ProgramManager& program_manager, GLuint texture, u32 input_image_width,
                 u32 input_image_height, const Common::Rectangle<f32>& crop_rect) {
    const f32 input_width = static_cast<f32>(input_image_width);
    const f32 input_height = static_cast<f32>(input_image_height);
    const f32 output_width = static_cast<f32>(width);
    const f32 output_height = static_cast<f32>(height);
    const f32 viewport_width = (crop_rect.right - crop_rect.left) * input_width;
    const f32 viewport_x = crop_rect.left * input_width;
    const f32 viewport_height = (crop_rect.bottom - crop_rect.top) * input_height;
    const f32 viewport_y = crop_rect.top * input_height;

    FsrConstants easu_con{};
    FsrConstants rcas_con{};

    FsrEasuConOffset(easu_con.data() + 0, easu_con.data() + 4, easu_con.data() + 8,
                     easu_con.data() + 12, viewport_width, viewport_height, input_width,
                     input_height, output_width, output_height, viewport_x, viewport_y);

    // UPGRADE [7]: centralized slider -> RCAS stops mapping (clamped 0-4 stops).
    FsrRcasCon(rcas_con.data(), GetSharpeningStops());

    // UPGRADE BUGFIX: the count argument of glProgramUniform4uiv is the number of
    // uvec4 elements (4), not the size in bytes (64) - the old code made the driver
    // read past the end of the constant array and raised a GL error.
    glProgramUniform4uiv(easu_frag.handle, 0, static_cast<GLsizei>(easu_con.size() / 4),
                         easu_con.data());
    glProgramUniform4uiv(rcas_frag.handle, 0, static_cast<GLsizei>(rcas_con.size() / 4),
                         rcas_con.data());

    glFrontFace(GL_CW);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.handle);
    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, easu_tex.handle, 0);
    glViewportIndexedf(0, 0.0f, 0.0f, output_width, output_height);
    program_manager.BindPresentPrograms(vert.handle, easu_frag.handle);
    glBindTextureUnit(0, texture);
    glBindSampler(0, sampler.handle);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glNamedFramebufferTexture(framebuffer.handle, GL_COLOR_ATTACHMENT0, rcas_tex.handle, 0);
    program_manager.BindPresentPrograms(vert.handle, rcas_frag.handle);
    glBindTextureUnit(0, easu_tex.handle);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    return rcas_tex.handle;
}

bool FSR::NeedsRecreation(const Common::Rectangle<u32>& screen) {
    return screen.GetWidth() != width || screen.GetHeight() != height;
}

} // namespace OpenGL
