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

    void add_button(const std::string &panel, const std::string &name, const std::function<void()> &callback);
    void add_repeat(const std::string &panel, const std::string &name, const std::function<bool()> &callback);

    int width() const;
    int height() const;

    Eigen::Matrix4f projection_matrix(float f = 1.0, float near = 1.0e-2, float far = 1.0e4);
    Eigen::Matrix4f view_matrix();
    Eigen::Matrix4f model_matrix();

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
