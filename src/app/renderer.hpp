#pragma once
#include "render/mc_mesher.hpp"
#include "app/gl_loader.hpp"
#include <array>

namespace vw::app {

using Mat4 = std::array<f32, 16>;
Mat4 mat_identity();
Mat4 mat_mul(const Mat4& a, const Mat4& b);
Mat4 mat_perspective(f32 fov_deg, f32 aspect, f32 znear, f32 zfar);
Mat4 mat_ortho(f32 l, f32 r, f32 b, f32 t, f32 zn, f32 zf);
Mat4 mat_look_at(const f32 eye[3], const f32 at[3], const f32 up[3]);

struct OrbitCamera {
    f32 target[3] = {0, 64, 0};
    f32 yaw = 0.8f, pitch = 0.6f, dist = 120.0f;
    bool ortho = false;
    f32 fov = 55.0f;

    void eye(f32 out[3]) const;
    Mat4 view() const;
    Mat4 proj(f32 aspect) const;
    void pan(f32 dx, f32 dy);
    void orbit(f32 dx, f32 dy);
    void zoom(f32 wheel);

    void fly(f32 fwd, f32 right, f32 up, f32 step);

    void look(f32 dx, f32 dy);
};

class MCRenderer {
public:
    bool init(bool gl33 = false);
    void upload(const render::MCMesh& mesh, const mesh::TextureAtlas& atlas);
    void draw(const Mat4& viewproj);
    void destroy();
    size_t triangle_count() const { return (opaque_count_ + alpha_count_) / 3; }

private:
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, ebo_ = 0, tex_ = 0;
    GLint u_mvp_ = -1, u_tex_ = -1, u_alpha_pass_ = -1;
    size_t opaque_count_ = 0, alpha_count_ = 0;
};

}
