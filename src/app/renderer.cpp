#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vw::app {

Mat4 mat_identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + rw] += a[k * 4 + rw] * b[c * 4 + k];
    return r;
}
Mat4 mat_perspective(f32 fov_deg, f32 aspect, f32 zn, f32 zf) {
    const f32 f = 1.0f / std::tan(fov_deg * 3.14159265f / 360.0f);
    Mat4 m{};
    m[0] = f / aspect; m[5] = f;
    m[10] = (zf + zn) / (zn - zf); m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
    return m;
}
Mat4 mat_ortho(f32 l, f32 r, f32 b, f32 t, f32 zn, f32 zf) {
    Mat4 m{};
    m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (zf - zn);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(zf + zn) / (zf - zn);
    m[15] = 1;
    return m;
}
static void norm3(f32 v[3]) {
    const f32 n = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (n > 0) { v[0] /= n; v[1] /= n; v[2] /= n; }
}
static void cross3(const f32 a[3], const f32 b[3], f32 o[3]) {
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}
Mat4 mat_look_at(const f32 eye[3], const f32 at[3], const f32 up[3]) {
    f32 fwd[3] = {at[0]-eye[0], at[1]-eye[1], at[2]-eye[2]};
    norm3(fwd);
    f32 right[3]; cross3(fwd, up, right); norm3(right);
    f32 u2[3]; cross3(right, fwd, u2);
    Mat4 m = mat_identity();
    m[0] = right[0]; m[4] = right[1]; m[8]  = right[2];
    m[1] = u2[0];    m[5] = u2[1];    m[9]  = u2[2];
    m[2] = -fwd[0];  m[6] = -fwd[1];  m[10] = -fwd[2];
    m[12] = -(right[0]*eye[0] + right[1]*eye[1] + right[2]*eye[2]);
    m[13] = -(u2[0]*eye[0] + u2[1]*eye[1] + u2[2]*eye[2]);
    m[14] =  (fwd[0]*eye[0] + fwd[1]*eye[1] + fwd[2]*eye[2]);
    return m;
}

void OrbitCamera::eye(f32 out[3]) const {
    out[0] = target[0] + dist * std::cos(pitch) * std::cos(yaw);
    out[1] = target[1] + dist * std::sin(pitch);
    out[2] = target[2] + dist * std::cos(pitch) * std::sin(yaw);
}
Mat4 OrbitCamera::view() const {
    f32 e[3]; eye(e);
    const f32 up[3] = {0, 1, 0};
    return mat_look_at(e, target, up);
}
Mat4 OrbitCamera::proj(f32 aspect) const {
    if (ortho) {
        const f32 h = dist * 0.5f, w = h * aspect;
        return mat_ortho(-w, w, -h, h, -4096.f, 4096.f);
    }
    return mat_perspective(fov, aspect, 0.5f, 8192.f);
}
void OrbitCamera::pan(f32 dx, f32 dy) {
    const f32 s = dist * 0.0022f;
    const f32 rx = -std::sin(yaw), rz = std::cos(yaw);
    target[0] += (rx * dx) * s;
    target[2] += (rz * dx) * s;
    target[1] += dy * s;
}
void OrbitCamera::orbit(f32 dx, f32 dy) {
    yaw += dx * 0.008f;
    pitch = std::clamp(pitch + dy * 0.008f, -1.55f, 1.55f);
}
void OrbitCamera::zoom(f32 wheel) {
    dist = std::clamp(dist * std::pow(0.9f, wheel), 2.0f, 4000.0f);
}
void OrbitCamera::fly(f32 fwd, f32 right, f32 up, f32 step) {
    f32 e[3]; eye(e);
    f32 f[3] = {target[0] - e[0], target[1] - e[1], target[2] - e[2]};
    const f32 fl = std::sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (fl <= 1e-6f) return;
    f[0] /= fl; f[1] /= fl; f[2] /= fl;

    f32 r[3] = {-f[2], 0.0f, f[0]};
    const f32 rl = std::sqrt(r[0]*r[0] + r[2]*r[2]);
    if (rl > 1e-6f) { r[0] /= rl; r[2] /= rl; }
    target[0] += (f[0] * fwd + r[0] * right) * step;
    target[1] += (f[1] * fwd) * step + up * step;
    target[2] += (f[2] * fwd + r[2] * right) * step;
}
void OrbitCamera::look(f32 dx, f32 dy) {
    f32 e[3]; eye(e);
    orbit(dx, dy);

    const f32 c = std::cos(pitch);
    const f32 dir[3] = {-c * std::cos(yaw), -std::sin(pitch), -c * std::sin(yaw)};
    target[0] = e[0] + dir[0] * dist;
    target[1] = e[1] + dir[1] * dist;
    target[2] = e[2] + dir[2] * dist;
}

static const char* kVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aCol;
uniform mat4 uMVP;
out vec2 vUV;
out vec3 vCol;
void main() {
    vUV = aUV;
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

static const char* kFS = R"(
in vec2 vUV;
in vec3 vCol;
uniform sampler2D uAtlas;
uniform int uAlphaPass;    // 0 = opaque (alpha-test), 1 = blend
out vec4 frag;
void main() {
    vec4 c = texture(uAtlas, vUV);
    if (uAlphaPass == 0) {
        if (c.a < 0.5) discard;
        frag = vec4(c.rgb * vCol, 1.0);
    } else {
        if (c.a < 0.02) discard;
        frag = vec4(c.rgb * vCol, clamp(c.a, 0.4, 0.85));
    }
})";

static GLuint compile(GLenum type, const char* src, const char* version_line) {
    GLuint s = glCreateShader(type);
    const char* parts[2] = {version_line, src};
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
    }
    return s;
}

bool MCRenderer::init(bool gl33) {
    const char* ver = gl33 ? "#version 330 core\n" : "#version 450 core\n";
    GLuint vs = compile(GL_VERTEX_SHADER, kVS, ver);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS, ver);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) return false;
    u_mvp_ = glGetUniformLocation(prog_, "uMVP");
    u_tex_ = glGetUniformLocation(prog_, "uAtlas");
    u_alpha_pass_ = glGetUniformLocation(prog_, "uAlphaPass");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenTextures(1, &tex_);
    return true;
}

void MCRenderer::upload(const render::MCMesh& mesh, const mesh::TextureAtlas& atlas) {
    static_assert(sizeof(render::MCVertex) == 11 * sizeof(f32));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(render::MCVertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);

    std::vector<u32> all;
    all.reserve(mesh.indices.size() + mesh.indices_alpha.size());
    all.insert(all.end(), mesh.indices.begin(), mesh.indices.end());
    all.insert(all.end(), mesh.indices_alpha.begin(), mesh.indices_alpha.end());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * 4),
                 all.data(), GL_STATIC_DRAW);
    opaque_count_ = mesh.indices.size();
    alpha_count_ = mesh.indices_alpha.size();

    const GLsizei stride = sizeof(render::MCVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, stride, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, 0, stride, reinterpret_cast<void*>(20));

    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.width(), atlas.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.pixels().data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

void MCRenderer::draw(const Mat4& viewproj) {
    if (!opaque_count_ && !alpha_count_) return;
    glUseProgram(prog_);
    glUniformMatrix4fv(u_mvp_, 1, 0, viewproj.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glUniform1i(u_tex_, 0);
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform1i(u_alpha_pass_, 0);
    glDisable(GL_BLEND);
    glDepthMask(1);
    if (opaque_count_)
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(opaque_count_),
                       GL_UNSIGNED_INT, nullptr);

    if (alpha_count_) {
        glUniform1i(u_alpha_pass_, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(alpha_count_), GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(opaque_count_ * 4));
        glDepthMask(1);
        glDisable(GL_BLEND);
    }
}

void MCRenderer::destroy() {
    // tex_ обязателен: иначе каждая загрузка мира утекает атлас в VRAM.
    // Обнуляем ID после удаления, чтобы повторный destroy() был безопасен.
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

}
