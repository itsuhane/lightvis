#ifndef LIGHTVIS_H
#define LIGHTVIS_H

#include <functional>
#include <memory>
#include <string>

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

  protected:
    virtual void draw(int w, int h);

  private:
    void draw_gui();
    void make_window_current();
    void process_events();
    void present();
    void create_window();
    void destroy_window();
    std::unique_ptr<LightVisDetail> detail;
};

int main();

} // namespace lightvis

#endif // LIGHTVIS_H
