#ifndef LIGHTVIS_H
#define LIGHTVIS_H

#include <functional>
#include <memory>
#include <string>
#include <Eigen/Eigen>
#include <lightvis/shader.h>

namespace lightvis {

class LightVisDetail;

struct MouseStates {
    bool mouse_left;
    bool mouse_middle;
    bool mouse_right;
    bool mouse_double_click;
    bool control_left;
    bool control_right;
    bool shift_left;
    bool shift_right;
    Eigen::Vector2f mouse_normal_position;
    Eigen::Vector2f mouse_drag_position;
    Eigen::Vector2f scroll;
};

class LightVis {
    friend class LightVisDetail;

  public:
    LightVis(const std::string &title, int width, int height);
    virtual ~LightVis();

    void show();
    void hide();

    int width() const;
    int height() const;

    const Eigen::Vector3f &location() const;
    Eigen::Vector3f &location();
    const float &scale() const;
    float &scale();

    Eigen::Matrix4f projection_matrix(float f = 1.0, float near = 1.0e-2, float far = 1.0e4);
    Eigen::Matrix4f view_matrix();
    Eigen::Matrix4f model_matrix();
    Shader *shader();

    void add_points(std::vector<Eigen::Vector3f> &points, Eigen::Vector4f &color);
    void add_points(std::vector<Eigen::Vector3f> &points, std::vector<Eigen::Vector4f> &colors);

    void add_trajectory(std::vector<Eigen::Vector3f> &positions, Eigen::Vector4f &color);
    void add_trajectory(std::vector<Eigen::Vector3f> &positions, std::vector<Eigen::Vector4f> &colors);

  protected:
    virtual void load();
    virtual void unload();
    virtual void draw(int w, int h);
    virtual bool mouse(const MouseStates &states);
    //virtual bool keyboard()
    virtual void gui(void *ctx, int w, int h);

  private:
    void activate_context();
    void process_events();
    void render_canvas();
    void render_gui();
    void present();
    void create_window();
    void destroy_window();
    std::unique_ptr<LightVisDetail> detail;
};

int main();

} // namespace lightvis

#endif // LIGHTVIS_H
