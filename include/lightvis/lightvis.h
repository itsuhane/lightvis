#ifndef LIGHTVIS_H
#define LIGHTVIS_H

#include <functional>
#include <memory>
#include <string>
#include <Eigen/Eigen>

namespace lightvis {

class LightVisDetail;

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

    void add_points(std::vector<Eigen::Vector3f> &points, Eigen::Vector4f &color);
    void add_points(std::vector<Eigen::Vector3f> &points, std::vector<Eigen::Vector4f> &colors);

    void add_trajectory(std::vector<Eigen::Vector3f> &positions, Eigen::Vector4f &color);
    void add_trajectory(std::vector<Eigen::Vector3f> &positions, std::vector<Eigen::Vector4f> &colors);

  protected:
    virtual void load();
    virtual void unload();
    virtual void draw(int w, int h);
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
