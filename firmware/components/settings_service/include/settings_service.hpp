#pragma once
#include "esp_err.h"
#include "monitor.hpp"
namespace monitor {
class SettingsService {
  public:
    explicit SettingsService(const char *namespace_name = "temp_monitor")
        : namespace_(namespace_name) {}
    esp_err_t load(Registry &registry);
    esp_err_t save(const Registry &registry);
    esp_err_t save_or_restore(Registry &candidate, const Registry &committed);
    bool writable() const {
        return writable_;
    }

  private:
    std::string previous_;
    const char *namespace_;
    bool writable_ = false;
};
} // namespace monitor
