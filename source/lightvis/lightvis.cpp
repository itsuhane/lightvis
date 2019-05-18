#include <lightvis/lightvis.h>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <Eigen/Eigen>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glbinding/gl/gl.h>
#include <glbinding/Binding.h>
#include <glbinding/glbinding.h>

#define NK_IMPLEMENTATION
#include <nuklear.h>

#include <lightvis/shader.h>
#include <lightvis/lightvis_font_roboto.h>

#define LIGHTVIS_DOUBLE_CLICK_MIN_DT 0.02
#define LIGHTVIS_DOUBLE_CLICK_MAX_DT 0.2

namespace lightvis {

struct vertex_t {
    float position[2];
    float texcoord[2];
    nk_byte color[4];
};

struct context_t {
    GLFWwindow *window;

    struct nk_context nuklear;
    struct nk_buffer commands;
    struct nk_font_atlas font_atlas;
    struct nk_draw_null_texture null_texture;

    gl::GLuint program;
    gl::GLuint vshader, fshader;
    gl::GLuint vbo, ebo, vao;
    gl::GLuint font_texture;

    gl::GLint attribute_position;
    gl::GLint attribute_texcoord;
    gl::GLint attribute_color;
    gl::GLint uniform_texture;
    gl::GLint uniform_projmat;
};

struct viewport_t {
    Eigen::Vector2i window_size;
    Eigen::Vector2i framebuffer_size;
    Eigen::Vector3f viewport_ypr = {-45, -42, 0};
    float viewport_distance = 15;
    Eigen::Vector3f world_xyz = {0, 0, 0};
    float scale = 1.0;
};

struct events_t {
    std::vector<unsigned int> characters;
    Eigen::Vector2f scroll_offset;
    bool double_click = false;
    Eigen::Vector2i double_click_position;
    double last_left_click_time = -std::numeric_limits<double>::max();
};

struct position_record_t {
    bool is_trajectory;
    const std::vector<Eigen::Vector3f> *data;
    const Eigen::Vector4f *color;
    const std::vector<Eigen::Vector4f> *colors;
};

std::set<LightVis *> &awaiting_windows() {
    static std::set<LightVis *> s_awaiting;
    return s_awaiting;
}

std::map<GLFWwindow *, LightVis *> &active_windows() {
    static std::map<GLFWwindow *, LightVis *> s_active;
    return s_active;
}

class LightVisDetail {
  public:
    std::string title;
    context_t context;
    viewport_t viewport;
    events_t events;

    MouseStates mouse_states;

    std::vector<position_record_t> position_records;

    std::unique_ptr<Shader> grid_shader;
    std::unique_ptr<Shader> position_shader;

    Eigen::Matrix4f projection_matrix(float f = 1.0, float near = 1.0e-2, float far = 1.0e4) const {
        Eigen::Matrix4f proj = Eigen::Matrix4f::Zero();
        proj(0, 0) = 2 * (f * viewport.framebuffer_size.y()) / viewport.framebuffer_size.x();
        proj(1, 1) = -2 * f;
        proj(2, 2) = (far + near) / (far - near);
        proj(2, 3) = 2 * far * near / (near - far);
        proj(3, 2) = 1.0;
        return proj;
    }

    Eigen::Matrix4f view_matrix() const {
        Eigen::Matrix4f view = Eigen::Matrix4f::Zero();
        view(0, 0) = 1.0;
        view(2, 1) = 1.0;
        view(1, 2) = -1.0;
        view(3, 3) = 1.0;
        return view;
    }

    Eigen::Matrix4f model_matrix() const {
        const Eigen::Vector3f &viewport_ypr = viewport.viewport_ypr;

        Eigen::Matrix3f R = Eigen::Matrix3f::Identity();
        R = Eigen::AngleAxisf(viewport_ypr[2] * M_PI / 180.0f, Eigen::Vector3f::UnitY()) * R; // r
        R = Eigen::AngleAxisf(viewport_ypr[1] * M_PI / 180.0f, Eigen::Vector3f::UnitX()) * R; // p
        R = Eigen::AngleAxisf(viewport_ypr[0] * M_PI / 180.0f, Eigen::Vector3f::UnitZ()) * R; // y

        double cy = cos(-viewport_ypr[0] * M_PI / 180.0f);
        double sy = sin(-viewport_ypr[0] * M_PI / 180.0f);
        double cp = cos(-viewport_ypr[1] * M_PI / 180.0f);
        double sp = sin(-viewport_ypr[1] * M_PI / 180.0f);
        Eigen::Vector3f viewport_xyz = {float(-sy * cp), float(-cy * cp), float(sp)};
        viewport_xyz *= viewport.viewport_distance;

        Eigen::Matrix4f world = Eigen::Matrix4f::Zero();
        world.block<3, 3>(0, 0) = R.transpose();
        world.block<3, 1>(0, 3) = -R.transpose() * viewport_xyz;
        world(3, 3) = 1.0;

        return world;
    }

    void load() {
        static const gl::GLchar *grid_vshader = R"(
            #version 150
            uniform mat4 ProjMat;
            uniform vec4 Color;
            in vec3 Position;
            out vec3 Frag_Position;
            out vec4 Frag_Color;
            void main() {
                Frag_Position = Position;
                Frag_Color = Color;
                gl_Position = ProjMat * vec4(Position, 1);
            }
        )";

        static const gl::GLchar *grid_fshader = R"(
            #version 150
            in vec3 Frag_Position;
            in vec4 Frag_Color;
            out vec4 Out_Color;
            void main(){
                vec3 r = 1.0 - smoothstep(9.5, 10.5, abs(Frag_Position));
                vec4 c = vec4(Frag_Color.rgb, Frag_Color.a * min(min(r.x, r.y), r.z));
                Out_Color = c;
            }
        )";

        static const gl::GLchar *position_vshader = R"(
            #version 150
            uniform mat4 ProjMat;
            uniform vec3 Location;
            uniform float Scale;
            in vec3 Position;
            in vec4 Color;
            out vec3 Frag_Position;
            out vec4 Frag_Color;
            void main() {
                vec3 p = Scale * (Position - Location);
                Frag_Position = p;
                Frag_Color = Color;
                gl_Position = ProjMat * vec4(p, 1);
            }
        )";

        static const gl::GLchar *position_fshader = R"(
            #version 150
            in vec3 Frag_Position;
            in vec4 Frag_Color;
            out vec4 Out_Color;
            void main(){
                vec3 r = 1.0 - smoothstep(9.5, 10.5, abs(Frag_Position));
                vec4 c = vec4(Frag_Color.rgb, Frag_Color.a * min(min(r.x, r.y), r.z));
                Out_Color = c;
            }
        )";

        grid_shader = std::make_unique<Shader>(grid_vshader, grid_fshader);
        position_shader = std::make_unique<Shader>(position_vshader, position_fshader);
    }

    void unload() {
        position_shader.reset();
        grid_shader.reset();
    }

    void gen_grid_level(int level, std::vector<Eigen::Vector3f> &grid_lines) const {
        const auto &scale = viewport.scale;
        const auto &location = viewport.world_xyz;
        double step = pow(10, -level);
        double gap = step * scale;

        float x0 = location.x() * scale;
        float y0 = location.y() * scale;
        float z0 = location.z() * scale;

        int x_lo = (int)ceil((-10.5 + x0) / gap);
        int x_hi = (int)floor((10.5 + x0) / gap);
        int y_lo = (int)ceil((-10.5 + y0) / gap);
        int y_hi = (int)floor((10.5 + y0) / gap);

        grid_lines.clear();

        for (int x = x_lo; x <= x_hi; ++x) {
            grid_lines.emplace_back(x * gap - x0, -10.5, -z0);
            grid_lines.emplace_back(x * gap - x0, 10.5, -z0);
        }
        for (int y = y_lo; y <= y_hi; ++y) {
            grid_lines.emplace_back(-10.5, y * gap - y0, -z0);
            grid_lines.emplace_back(10.5, y * gap - y0, -z0);
        }
    }

    void draw_grid() {
        static std::vector<Eigen::Vector3f> bounding_box_vertices{
            {10, 10, 10},
            {-10, 10, 10},
            {-10, -10, 10},
            {10, -10, 10},
            {10, -10, -10},
            {-10, -10, -10},
            {-10, 10, -10},
            {10, 10, -10}};
        static std::vector<unsigned int> bounding_box_edges{
            0, 1, 1, 2, 2, 3, 3, 4,
            4, 5, 5, 6, 6, 7, 0, 7,
            0, 3, 4, 7, 1, 6, 2, 5};

        gl::glDisable(gl::GL_SCISSOR_TEST);
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glEnable(gl::GL_BLEND);
        gl::glBlendEquation(gl::GL_FUNC_ADD);
        gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);

        grid_shader->bind();
        grid_shader->set_uniform("ProjMat", Eigen::Matrix4f(projection_matrix() * view_matrix() * model_matrix()));

        grid_shader->set_uniform("Color", Eigen::Vector4f{1.0, 1.0, 1.0, 0.25});
        grid_shader->set_attribute("Position", bounding_box_vertices);
        grid_shader->set_indices(bounding_box_edges);
        grid_shader->draw_indexed(gl::GL_LINES, 0, 24);

        double level = log10f(viewport.scale * 5);
        static std::vector<Eigen::Vector3f> grid_lines;

        gen_grid_level(floor(level) - 1, grid_lines);
        grid_shader->set_uniform("Color", Eigen::Vector4f{1.0, 1.0, 1.0, 0.25});
        grid_shader->set_attribute("Position", grid_lines);
        grid_shader->draw(gl::GL_LINES, 0, grid_lines.size());

        gen_grid_level(floor(level), grid_lines);
        grid_shader->set_uniform("Color", Eigen::Vector4f{1.0, 1.0, 1.0, float(pow(level - floor(level), 0.9) * 0.25)});
        grid_shader->set_attribute("Position", grid_lines);
        grid_shader->draw(gl::GL_LINES, 0, grid_lines.size());

        grid_shader->unbind();
    }

    void draw_positions() {
        gl::glDisable(gl::GL_DEPTH_TEST);
        position_shader->bind();
        position_shader->set_uniform("ProjMat", Eigen::Matrix4f(projection_matrix() * view_matrix() * model_matrix()));
        position_shader->set_uniform("Location", viewport.world_xyz);
        position_shader->set_uniform("Scale", viewport.scale);
        for (const auto &record : position_records) {
            if (record.data->empty()) continue;
            static std::vector<Eigen::Vector4f> colors;
            colors.resize(record.data->size());
            if (record.color) {
                std::fill(colors.begin(), colors.end(), *record.color);
            } else if (record.colors) {
                std::copy(record.colors->begin(), record.colors->end(), colors.begin());
            }
            position_shader->set_attribute("Position", *record.data);
            position_shader->set_attribute("Color", colors);
            if (record.is_trajectory) {
                position_shader->draw(gl::GL_LINE_STRIP, 0, record.data->size());
            } else {
                position_shader->draw(gl::GL_POINTS, 0, record.data->size());
            }
        }
        position_shader->unbind();
    }

    static void error_callback(int error, const char *description) {
        fprintf(stderr, "GLFW Error: %s\n", description);
    }

    static void mouse_input_callback(GLFWwindow *win, int button, int action, int mods) {
        auto &events = active_windows().at(win)->detail->events;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                double current_button_time = glfwGetTime();
                double dt = current_button_time - events.last_left_click_time;
                if (dt > LIGHTVIS_DOUBLE_CLICK_MIN_DT && dt < LIGHTVIS_DOUBLE_CLICK_MAX_DT) {
                    double x, y;
                    glfwGetCursorPos(win, &x, &y);
                    events.double_click = true;
                    events.double_click_position = Eigen::Vector2d(x, y).cast<int>();
                    events.last_left_click_time = -std::numeric_limits<double>::max();
                } else {
                    events.last_left_click_time = current_button_time;
                }
            }
        }
    }

    static void scroll_input_callback(GLFWwindow *win, double dx, double dy) {
        auto &events = active_windows().at(win)->detail->events;
        events.scroll_offset += Eigen::Vector2f((float)dx, (float)dy);
    }

    static void character_input_callback(GLFWwindow *win, unsigned int codepoint) {
        auto &characters = active_windows().at(win)->detail->events.characters;
        if (characters.size() < NK_INPUT_MAX) {
            characters.push_back(codepoint);
        }
    }

    static void clipboard_copy_callback(nk_handle usr, const char *text, int len) {
        if (len == 0) return;
        std::vector<char> str(text, text + len);
        str.push_back('\0');
        glfwSetClipboardString((GLFWwindow *)usr.ptr, str.data());
    }

    static void clipboard_paste_callback(nk_handle usr, struct nk_text_edit *edit) {
        if (const char *text = glfwGetClipboardString((GLFWwindow *)usr.ptr)) {
            nk_textedit_paste(edit, text, nk_strlen(text));
        }
    }

    static void window_refresh_callback(GLFWwindow *win) {
        auto vis = active_windows().at(win);
        vis->activate_context();
        vis->gui(&vis->detail->context.nuklear, vis->detail->viewport.window_size.x(), vis->detail->viewport.window_size.y());
        vis->render_canvas();
        vis->render_gui();
        vis->present();
    }

    static int main() {
        glfwInit();
        glfwSetErrorCallback(error_callback);
        while (!active_windows().empty() || !awaiting_windows().empty()) {
            /* spawn windows */ {
                for (auto vis : awaiting_windows()) {
                    vis->create_window();
                }
                awaiting_windows().clear();
            }

            glfwPollEvents();

            /* close windows */ {
                std::vector<LightVis *> closing;
                for (auto [glfw, vis] : active_windows()) {
                    if (glfwWindowShouldClose(glfw)) {
                        closing.emplace_back(vis);
                    }
                }
                for (auto vis : closing) {
                    vis->hide();
                }
            }

            /* handle window events */ {
                for (auto [glfw, vis] : active_windows()) {
                    vis->activate_context();
                    vis->process_events();
                    vis->gui(&vis->detail->context.nuklear, vis->detail->viewport.window_size.x(), vis->detail->viewport.window_size.y());
                    vis->render_canvas();
                    vis->render_gui();
                    vis->present();
                }
            }
        }
        glfwTerminate();
        return EXIT_SUCCESS;
    }
}; // namespace lightvis

LightVis::LightVis(const std::string &title, int width, int height) {
    detail = std::make_unique<LightVisDetail>();
    detail->title = title;
    detail->viewport.window_size = {width, height};
    memset(&detail->context, 0, sizeof(context_t));
}

LightVis::~LightVis() {
    hide();
}

void LightVis::show() {
    if (!detail->context.window) {
        awaiting_windows().insert(this);
    }
}

void LightVis::hide() {
    if (detail->context.window) {
        destroy_window();
    }
}

int LightVis::width() const {
    return detail->viewport.window_size.x();
}

int LightVis::height() const {
    return detail->viewport.window_size.y();
}

const Eigen::Vector3f &LightVis::location() const {
    return detail->viewport.world_xyz;
}

Eigen::Vector3f &LightVis::location() {
    return detail->viewport.world_xyz;
}

const float &LightVis::scale() const {
    return detail->viewport.scale;
}

float &LightVis::scale() {
    return detail->viewport.scale;
}

Eigen::Matrix4f LightVis::projection_matrix(float f, float near, float far) {
    return detail->projection_matrix(f, near, far);
}

Eigen::Matrix4f LightVis::view_matrix() {
    return detail->view_matrix();
}

Eigen::Matrix4f LightVis::model_matrix() {
    return detail->model_matrix();
}

void LightVis::add_points(std::vector<Eigen::Vector3f> &points, Eigen::Vector4f &color) {
    position_record_t record;
    record.is_trajectory = false;
    record.data = &points;
    record.color = &color;
    record.colors = nullptr;
    detail->position_records.push_back(record);
}

void LightVis::add_points(std::vector<Eigen::Vector3f> &points, std::vector<Eigen::Vector4f> &colors) {
    position_record_t record;
    record.is_trajectory = false;
    record.data = &points;
    record.color = nullptr;
    record.colors = &colors;
    detail->position_records.push_back(record);
}

void LightVis::add_trajectory(std::vector<Eigen::Vector3f> &positions, Eigen::Vector4f &color) {
    position_record_t record;
    record.is_trajectory = true;
    record.data = &positions;
    record.color = &color;
    record.colors = nullptr;
    detail->position_records.push_back(record);
}

void LightVis::add_trajectory(std::vector<Eigen::Vector3f> &positions, std::vector<Eigen::Vector4f> &colors) {
    position_record_t record;
    record.is_trajectory = true;
    record.data = &positions;
    record.color = nullptr;
    record.colors = &colors;
    detail->position_records.push_back(record);
}

void LightVis::load() {
}

void LightVis::unload() {
}

void LightVis::draw(int w, int h) {
}

bool LightVis::mouse(const MouseStates &states) {
    return false;
}

void LightVis::gui(void *ctx, int w, int h) {
}

void LightVis::activate_context() {
    glfwMakeContextCurrent(detail->context.window);
    glbinding::useCurrentContext();
    glfwGetWindowSize(detail->context.window, &detail->viewport.window_size.x(), &detail->viewport.window_size.y());
    glfwGetFramebufferSize(detail->context.window, &detail->viewport.framebuffer_size.x(), &detail->viewport.framebuffer_size.y());
}

void LightVis::process_events() {
    auto window = detail->context.window;
    auto nuklear = &detail->context.nuklear;
    auto &events = detail->events;

    nk_input_begin(nuklear);

    for (const auto &character : events.characters) {
        nk_input_unicode(nuklear, character);
    }
    events.characters.clear();

    nk_input_key(nuklear, NK_KEY_DEL, glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_ENTER, glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_TAB, glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_BACKSPACE, glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_UP, glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_DOWN, glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_TEXT_START, glfwGetKey(window, GLFW_KEY_HOME) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_TEXT_END, glfwGetKey(window, GLFW_KEY_END) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_SCROLL_START, glfwGetKey(window, GLFW_KEY_HOME) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_SCROLL_END, glfwGetKey(window, GLFW_KEY_END) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_SCROLL_DOWN, glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS);
    nk_input_key(nuklear, NK_KEY_SCROLL_UP, glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS);

    bool shift_left = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    bool shift_right = (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    bool control_left = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
    bool control_right = (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    bool shift_down = shift_left || shift_right;
    bool control_down = control_left || control_right;
    nk_input_key(nuklear, NK_KEY_SHIFT, shift_down);
    nk_input_key(nuklear, NK_KEY_CTRL, control_down);

    if (control_down) {
        if (shift_down) {
            nk_input_key(nuklear, NK_KEY_TEXT_REDO, glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
        } else {
            nk_input_key(nuklear, NK_KEY_COPY, glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS);
            nk_input_key(nuklear, NK_KEY_PASTE, glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS);
            nk_input_key(nuklear, NK_KEY_CUT, glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS);
            nk_input_key(nuklear, NK_KEY_TEXT_UNDO, glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
            nk_input_key(nuklear, NK_KEY_TEXT_WORD_LEFT, glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
            nk_input_key(nuklear, NK_KEY_TEXT_WORD_RIGHT, glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS);
        }
    } else {
        nk_input_key(nuklear, NK_KEY_LEFT, glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
        nk_input_key(nuklear, NK_KEY_RIGHT, glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS);
    }

    bool button_left = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    bool button_middle = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);
    bool button_right = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    nk_input_motion(nuklear, (int)x, (int)y);
    nk_input_button(nuklear, NK_BUTTON_LEFT, (int)x, (int)y, (int)button_left);
    nk_input_button(nuklear, NK_BUTTON_MIDDLE, (int)x, (int)y, (int)button_middle);
    nk_input_button(nuklear, NK_BUTTON_RIGHT, (int)x, (int)y, (int)button_right);
    nk_input_button(nuklear, NK_BUTTON_DOUBLE, events.double_click_position.x(), events.double_click_position.y(), events.double_click);
    nk_input_scroll(nuklear, nk_vec2(events.scroll_offset.x(), events.scroll_offset.y()));

    nk_input_end(nuklear);

    if (!nk_item_is_any_active(nuklear)) {
        MouseStates &states = detail->mouse_states;
        states.mouse_left = button_left;
        states.mouse_middle = button_middle;
        states.mouse_right = button_right;
        states.mouse_double_click = events.double_click;
        states.scroll = events.scroll_offset;
        if (!(states.mouse_left || states.mouse_middle || states.mouse_right)) {
            states.mouse_normal_position = {x, y};
        }
        states.mouse_drag_position = {x, y};
        states.control_left = control_left;
        states.control_right = control_right;
        states.shift_left = shift_left;
        states.shift_right = shift_right;
        if (!mouse(states)) {
            static Eigen::Vector3f last_ypr;
            if (!(states.mouse_left || states.mouse_middle || states.mouse_right)) {
                last_ypr = detail->viewport.viewport_ypr;
            }
            Eigen::Vector2f drag = states.mouse_drag_position - states.mouse_normal_position;
            detail->viewport.viewport_ypr.x() = last_ypr.x() - drag.x() / 10;
            detail->viewport.viewport_ypr.y() = last_ypr.y() - drag.y() / 10;
            detail->viewport.scale = std::clamp(detail->viewport.scale * (1.0 + events.scroll_offset.y() / 600.0), 1.0e-4, 1.0e4);
        }
    }

    events.double_click = false;
    events.scroll_offset.setZero();
}

void LightVis::render_canvas() {
    int w = detail->viewport.framebuffer_size.x();
    int h = detail->viewport.framebuffer_size.y();
    gl::glViewport(0, 0, w, h);
    gl::glClearColor(0.125, 0.125, 0.125, 1.0); // TODO
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    gl::glPointSize(3);
    detail->draw_grid();
    detail->draw_positions();
}

void LightVis::render_gui() {
    auto &context = detail->context;
    auto &viewport = detail->viewport;

    gl::GLfloat ortho[4][4] = {
        {2.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, -2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}};
    ortho[0][0] /= (gl::GLfloat)detail->viewport.window_size.x();
    ortho[1][1] /= (gl::GLfloat)detail->viewport.window_size.y();

    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_DEPTH_TEST);

    gl::glEnable(gl::GL_BLEND);
    gl::glEnable(gl::GL_SCISSOR_TEST);

    gl::glBlendEquation(gl::GL_FUNC_ADD);
    gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);

    gl::glUseProgram(context.program);

    gl::glUniform1i(context.uniform_texture, 0);
    gl::glUniformMatrix4fv(context.uniform_projmat, 1, gl::GL_FALSE, &ortho[0][0]);
    gl::glViewport(0, 0, viewport.framebuffer_size.x(), viewport.framebuffer_size.y());

    gl::glBindVertexArray(context.vao);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, context.vbo);
    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, context.ebo);

    nk_convert_config config;
    memset(&config, 0, sizeof(nk_convert_config));

    static const nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(vertex_t, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(vertex_t, texcoord)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(vertex_t, color)},
        {NK_VERTEX_LAYOUT_END}};

    config.vertex_layout = vertex_layout;
    config.vertex_size = sizeof(vertex_t);
    config.vertex_alignment = NK_ALIGNOF(vertex_t);
    config.null = context.null_texture;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;

    nk_buffer vbuffer, ebuffer;
    nk_buffer_init_default(&vbuffer);
    nk_buffer_init_default(&ebuffer);
    nk_convert(&context.nuklear, &context.commands, &vbuffer, &ebuffer, &config);

    gl::glBufferData(gl::GL_ARRAY_BUFFER, nk_buffer_total(&vbuffer), nullptr, gl::GL_STREAM_DRAW);
    gl::glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER, nk_buffer_total(&ebuffer), nullptr, gl::GL_STREAM_DRAW);

    void *vertices = gl::glMapBuffer(gl::GL_ARRAY_BUFFER, gl::GL_WRITE_ONLY);
    void *elements = gl::glMapBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, gl::GL_WRITE_ONLY);

    memcpy(vertices, nk_buffer_memory(&vbuffer), nk_buffer_total(&vbuffer));
    memcpy(elements, nk_buffer_memory(&ebuffer), nk_buffer_total(&ebuffer));

    gl::glUnmapBuffer(gl::GL_ARRAY_BUFFER);
    gl::glUnmapBuffer(gl::GL_ELEMENT_ARRAY_BUFFER);

    nk_buffer_free(&ebuffer);
    nk_buffer_free(&vbuffer);

    Eigen::Vector2f framebuffer_scale = viewport.framebuffer_size.cast<float>().array() / viewport.window_size.cast<float>().array();
    const nk_draw_command *command;
    const nk_draw_index *offset = nullptr;
    nk_draw_foreach(command, &context.nuklear, &context.commands) {
        if (command->elem_count == 0) continue;
        gl::glBindTexture(gl::GL_TEXTURE_2D, (gl::GLuint)command->texture.id);
        gl::glScissor(
            (gl::GLint)(command->clip_rect.x * framebuffer_scale.x()),
            (gl::GLint)((viewport.window_size.y() - (command->clip_rect.y + command->clip_rect.h)) * framebuffer_scale.y()),
            (gl::GLint)(command->clip_rect.w * framebuffer_scale.x()),
            (gl::GLint)(command->clip_rect.h * framebuffer_scale.y()));
        gl::glDrawElements(gl::GL_TRIANGLES, (gl::GLsizei)command->elem_count, gl::GL_UNSIGNED_SHORT, offset);
        offset += command->elem_count;
    }
    nk_clear(&context.nuklear);

    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, 0);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
    gl::glBindVertexArray(0);

    gl::glUseProgram(0);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glDisable(gl::GL_BLEND);
}

void LightVis::present() {
    glfwSwapBuffers(detail->context.window);
}

void LightVis::create_window() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, (int)gl::GL_TRUE);
#endif

    if ((detail->context.window = glfwCreateWindow(detail->viewport.window_size.x(), detail->viewport.window_size.y(), detail->title.c_str(), nullptr, nullptr)) == nullptr) return;

    active_windows()[detail->context.window] = this;
    glfwMakeContextCurrent(detail->context.window);
    glbinding::initialize(glfwGetProcAddress, false);
    glfwGetFramebufferSize(detail->context.window, &detail->viewport.framebuffer_size.x(), &detail->viewport.framebuffer_size.y());
    glfwSwapInterval(1);

    nk_init_default(&detail->context.nuklear, 0);
    detail->context.nuklear.clip.copy = LightVisDetail::clipboard_copy_callback;
    detail->context.nuklear.clip.paste = LightVisDetail::clipboard_paste_callback;
    detail->context.nuklear.clip.userdata = nk_handle_ptr(detail->context.window);

    nk_buffer_init_default(&detail->context.commands);

    static const gl::GLchar *vshader = R"(
        #version 150
        uniform mat4 ProjMat;
        in vec2 Position;
        in vec2 TexCoord;
        in vec4 Color;
        out vec2 Frag_UV;
        out vec4 Frag_Color;
        void main() {
            Frag_UV = TexCoord;
            Frag_Color = Color;
            gl_Position = ProjMat * vec4(Position.xy, 0, 1);
        }
    )";

    static const gl::GLchar *fshader = R"(
        #version 150
        precision mediump float;
        uniform sampler2D Texture;
        in vec2 Frag_UV;
        in vec4 Frag_Color;
        out vec4 Out_Color;
        void main(){
            Out_Color = Frag_Color * texture(Texture, Frag_UV.st);
        }
    )";

    detail->context.vshader = gl::glCreateShader(gl::GL_VERTEX_SHADER);
    detail->context.fshader = gl::glCreateShader(gl::GL_FRAGMENT_SHADER);
    gl::glShaderSource(detail->context.vshader, 1, &vshader, 0);
    gl::glShaderSource(detail->context.fshader, 1, &fshader, 0);
    gl::glCompileShader(detail->context.vshader);
    gl::glCompileShader(detail->context.fshader);

    detail->context.program = gl::glCreateProgram();
    gl::glAttachShader(detail->context.program, detail->context.vshader);
    gl::glAttachShader(detail->context.program, detail->context.fshader);
    gl::glLinkProgram(detail->context.program);

    detail->context.uniform_texture = gl::glGetUniformLocation(detail->context.program, "Texture");
    detail->context.uniform_projmat = gl::glGetUniformLocation(detail->context.program, "ProjMat");
    detail->context.attribute_position = gl::glGetAttribLocation(detail->context.program, "Position");
    detail->context.attribute_texcoord = gl::glGetAttribLocation(detail->context.program, "TexCoord");
    detail->context.attribute_color = gl::glGetAttribLocation(detail->context.program, "Color");

    gl::glGenVertexArrays(1, &detail->context.vao);
    gl::glGenBuffers(1, &detail->context.vbo);
    gl::glGenBuffers(1, &detail->context.ebo);

    gl::glBindVertexArray(detail->context.vao);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, detail->context.vbo);
    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, detail->context.ebo);

    gl::glEnableVertexAttribArray((gl::GLuint)detail->context.attribute_position);
    gl::glEnableVertexAttribArray((gl::GLuint)detail->context.attribute_texcoord);
    gl::glEnableVertexAttribArray((gl::GLuint)detail->context.attribute_color);

    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_position, 2, gl::GL_FLOAT, gl::GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, position));
    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_texcoord, 2, gl::GL_FLOAT, gl::GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, texcoord));
    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_color, 4, gl::GL_UNSIGNED_BYTE, gl::GL_TRUE, sizeof(vertex_t), (void *)offsetof(vertex_t, color));

    gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, 0);
    gl::glBindVertexArray(0);

    const void *font_image;
    int font_image_width, font_image_height;
    nk_font_atlas_init_default(&detail->context.font_atlas);
    nk_font_atlas_begin(&detail->context.font_atlas);
    nk_font *roboto = nk_font_atlas_add_from_memory(&detail->context.font_atlas, Roboto_Regular_ttf, Roboto_Regular_ttf_len, 16, nullptr);
    font_image = nk_font_atlas_bake(&detail->context.font_atlas, &font_image_width, &font_image_height, NK_FONT_ATLAS_RGBA32);
    gl::glGenTextures(1, &detail->context.font_texture);
    gl::glBindTexture(gl::GL_TEXTURE_2D, detail->context.font_texture);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
    gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, gl::GL_RGBA, (gl::GLsizei)font_image_width, (gl::GLsizei)font_image_height, 0, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, font_image);
    nk_font_atlas_end(&detail->context.font_atlas, nk_handle_id((int)detail->context.font_texture), &detail->context.null_texture);
    nk_style_set_font(&detail->context.nuklear, &roboto->handle);

    glfwSetMouseButtonCallback(detail->context.window, LightVisDetail::mouse_input_callback);
    glfwSetScrollCallback(detail->context.window, LightVisDetail::scroll_input_callback);
    glfwSetCharCallback(detail->context.window, LightVisDetail::character_input_callback);

    glfwSetWindowRefreshCallback(detail->context.window, LightVisDetail::window_refresh_callback);

    detail->load();
    load();
}

void LightVis::destroy_window() {
    activate_context();

    unload();
    detail->unload();

    glfwSetWindowRefreshCallback(detail->context.window, nullptr);
    glfwSetCharCallback(detail->context.window, nullptr);
    glfwSetScrollCallback(detail->context.window, nullptr);
    glfwSetMouseButtonCallback(detail->context.window, nullptr);

    gl::glDeleteTextures(1, &detail->context.font_texture);
    nk_font_atlas_clear(&detail->context.font_atlas);

    gl::glDeleteBuffers(1, &detail->context.ebo);
    gl::glDeleteBuffers(1, &detail->context.vbo);
    gl::glDeleteVertexArrays(1, &detail->context.vao);
    gl::glDetachShader(detail->context.program, detail->context.fshader);
    gl::glDetachShader(detail->context.program, detail->context.vshader);
    gl::glDeleteProgram(detail->context.program);
    gl::glDeleteShader(detail->context.fshader);
    gl::glDeleteShader(detail->context.vshader);

    nk_buffer_free(&detail->context.commands);

    nk_free(&detail->context.nuklear);

    active_windows().erase(detail->context.window);
    glfwDestroyWindow(detail->context.window);

    memset(&detail->context, 0, sizeof(context_t));
}

int main() {
    return LightVisDetail::main();
}

} // namespace lightvis
