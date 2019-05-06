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

#define LIGHTVIS_DOUBLE_CLICK_MIN_DT 0.02
#define LIGHTVIS_DOUBLE_CLICK_MAX_DT 0.2

namespace lightvis {

struct nk_vertex_t {
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
};

struct events_t {
    std::vector<unsigned int> characters;
    Eigen::Vector2f scroll_offset;
    Eigen::Vector2i mouse_position;
    bool left_click = false;
    bool middle_click = false;
    bool right_click = false;
    bool double_click = false;
    Eigen::Vector2i double_click_position;
    double last_left_click_time = -std::numeric_limits<double>::max();
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

    static void error_callback(int error, const char *description) {
        fprintf(stderr, "GLFW Error: %s\n", description);
    }

    static void mouse_input_callback(GLFWwindow *win, int button, int action, int mods) {
        auto &events = active_windows().at(win)->detail->events;
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        events.mouse_position = Eigen::Vector2i((int)x, (int)y);
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            events.left_click = (action == GLFW_PRESS);
            if (action == GLFW_PRESS) {
                double current_button_time = glfwGetTime();
                double dt = current_button_time - events.last_left_click_time;
                if (dt > LIGHTVIS_DOUBLE_CLICK_MIN_DT && dt < LIGHTVIS_DOUBLE_CLICK_MAX_DT) {
                    puts("dbl click");
                    events.double_click = true;
                    events.double_click_position = events.mouse_position;
                    events.last_left_click_time = -std::numeric_limits<double>::max();
                } else {
                    events.last_left_click_time = current_button_time;
                }
            }
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            events.middle_click = (action == GLFW_PRESS);
            events.double_click = false;
            events.last_left_click_time = -std::numeric_limits<double>::max();
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            events.right_click = (action == GLFW_PRESS);
            events.double_click = false;
            events.last_left_click_time = -std::numeric_limits<double>::max();
        }
    }

    static void scroll_input_callback(GLFWwindow *win, double dx, double dy) {
        auto &events = active_windows().at(win)->detail->events;
        events.scroll_offset += Eigen::Vector2f((float)dx, (float)dy);
    }

    static void character_input_callback(GLFWwindow *win, unsigned int codepoint) {
        active_windows().at(win)->detail->events.characters.push_back(codepoint);
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

            /* handle window events */ {
                std::vector<LightVis *> closing;
                for (auto [glfw, vis] : active_windows()) {
                    if (glfwWindowShouldClose(glfw)) {
                        closing.emplace_back(vis);
                    } else {
                        vis->make_window_current();
                        vis->process_events();
                        vis->draw(vis->detail->viewport.window_size.x(), vis->detail->viewport.window_size.y());
                        vis->draw_gui();
                        vis->present();
                    }
                }
                for (auto vis : closing) {
                    vis->hide();
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

void LightVis::draw(int w, int h) {
}

void LightVis::draw_gui() {
}

void LightVis::make_window_current() {
    glfwMakeContextCurrent(detail->context.window);
    glbinding::useContext((glbinding::ContextHandle)detail->context.window);
    glfwGetWindowSize(detail->context.window, &detail->viewport.window_size.x(), &detail->viewport.window_size.y());
    glfwGetFramebufferSize(detail->context.window, &detail->viewport.framebuffer_size.x(), &detail->viewport.framebuffer_size.y());
}

void LightVis::process_events() {
    auto nuklear = &detail->context.nuklear;
    auto &events = detail->events;
    nk_input_begin(nuklear);

    for (const auto &character : events.characters) {
        nk_input_unicode(nuklear, character);
    }
    events.characters.clear();

    // TODO: handle key events

    int x = (int)events.mouse_position.x();
    int y = (int)events.mouse_position.y();
    nk_input_motion(nuklear, x, y);
    nk_input_button(nuklear, NK_BUTTON_LEFT, x, y, events.left_click);
    nk_input_button(nuklear, NK_BUTTON_MIDDLE, x, y, events.middle_click);
    nk_input_button(nuklear, NK_BUTTON_RIGHT, x, y, events.right_click);

    nk_input_button(nuklear, NK_BUTTON_DOUBLE, events.double_click_position.x(), events.double_click_position.y(), events.double_click);
    events.double_click = false;

    nk_input_scroll(nuklear, nk_vec2(events.scroll_offset.x(), events.scroll_offset.y()));
    events.scroll_offset.setZero();
}

void LightVis::present() {
    glfwSwapBuffers(detail->context.window);
}

void LightVis::create_window() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, (int)gl::GL_TRUE);
#endif

    if ((detail->context.window = glfwCreateWindow(detail->viewport.window_size.x(), detail->viewport.window_size.y(), detail->title.c_str(), nullptr, nullptr)) == nullptr) return;

    active_windows()[detail->context.window] = this;
    glfwMakeContextCurrent(detail->context.window);
    glfwGetFramebufferSize(detail->context.window, &detail->viewport.framebuffer_size.x(), &detail->viewport.framebuffer_size.y());
    glfwSwapInterval(1);
    glbinding::Binding::initialize((glbinding::ContextHandle)detail->context.window, glfwGetProcAddress, true, false);

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

    gl::GLint status;
    detail->context.vshader = gl::glCreateShader(gl::GL_VERTEX_SHADER);
    detail->context.fshader = gl::glCreateShader(gl::GL_FRAGMENT_SHADER);
    gl::glShaderSource(detail->context.vshader, 1, &vshader, 0);
    gl::glShaderSource(detail->context.fshader, 1, &fshader, 0);
    gl::glCompileShader(detail->context.vshader);
    gl::glCompileShader(detail->context.fshader);

    gl::glGetShaderiv(detail->context.vshader, gl::GL_COMPILE_STATUS, &status);
    gl::glGetShaderiv(detail->context.fshader, gl::GL_COMPILE_STATUS, &status);

    detail->context.program = gl::glCreateProgram();
    gl::glAttachShader(detail->context.program, detail->context.vshader);
    gl::glAttachShader(detail->context.program, detail->context.fshader);
    gl::glLinkProgram(detail->context.program);
    gl::glGetProgramiv(detail->context.program, gl::GL_LINK_STATUS, &status);

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

    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_position, 2, gl::GL_FLOAT, gl::GL_FALSE, sizeof(nk_vertex_t), (void *)offsetof(nk_vertex_t, position));
    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_texcoord, 2, gl::GL_FLOAT, gl::GL_FALSE, sizeof(nk_vertex_t), (void *)offsetof(nk_vertex_t, texcoord));
    gl::glVertexAttribPointer((gl::GLuint)detail->context.attribute_color, 4, gl::GL_UNSIGNED_BYTE, gl::GL_TRUE, sizeof(nk_vertex_t), (void *)offsetof(nk_vertex_t, color));

    gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, 0);
    gl::glBindVertexArray(0);

    const void *font_image;
    int font_image_width, font_image_height;
    nk_font_atlas_init_default(&detail->context.font_atlas);
    nk_font_atlas_begin(&detail->context.font_atlas);
    font_image = nk_font_atlas_bake(&detail->context.font_atlas, &font_image_width, &font_image_height, NK_FONT_ATLAS_RGBA32);
    gl::glGenTextures(1, &detail->context.font_texture);
    gl::glBindTexture(gl::GL_TEXTURE_2D, detail->context.font_texture);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
    gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, gl::GL_RGBA, (gl::GLsizei)font_image_width, (gl::GLsizei)font_image_height, 0, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, font_image);

    nk_font_atlas_end(&detail->context.font_atlas, nk_handle_id((int)detail->context.font_texture), &detail->context.null_texture);
    if (detail->context.font_atlas.default_font) {
        nk_style_set_font(&detail->context.nuklear, &detail->context.font_atlas.default_font->handle);
    }

    glfwSetMouseButtonCallback(detail->context.window, LightVisDetail::mouse_input_callback);
    glfwSetScrollCallback(detail->context.window, LightVisDetail::scroll_input_callback);
    glfwSetCharCallback(detail->context.window, LightVisDetail::character_input_callback);
}

void LightVis::destroy_window() {
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
