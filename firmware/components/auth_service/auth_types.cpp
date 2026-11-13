#include "auth_types.hpp"
namespace monitor {
EnrollReply enrollment_reply(EnrollResult result) {
    using Result = EnrollResult;
    switch (result) {
    case Result::enrolled:
        return {"201 Created", "", 0};
    case Result::already_enrolled:
        return {"409 Conflict", "An administrator password already exists", 0};
    case Result::window_closed:
        return {"403 Forbidden", "Hold BOOT for five seconds to open setup, then try again", 0};
    case Result::too_soon:
        return {"429 Too Many Requests", "Wait five seconds and try again", 5};
    case Result::bad_password:
        return {"400 Bad Request", "The password must be 12 to 128 characters", 0};
    case Result::unavailable:
        break;
    }
    return {"503 Service Unavailable",
            "The device could not save the password just now; try again in a few seconds", 5};
}
} // namespace monitor
