#include "storage.hpp"
#include "app.hpp"

extern "C" void app_main(void)
{
    Storage::init();
    App::run();
}
