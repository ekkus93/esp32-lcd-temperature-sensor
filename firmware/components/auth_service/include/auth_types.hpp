#pragma once
// Authentication results as plain values (no FreeRTOS types), shared by AuthService and the HTTP
// API ports.
namespace monitor {
// Why an enrollment succeeded or was refused (see AuthService::enroll()).
enum class EnrollResult {
    enrolled,
    already_enrolled, // an administrator password exists
    window_closed,    // the setup window is not open
    too_soon,         // within five seconds of the previous attempt
    bad_password,     // not 12 to 128 characters; not counted as an attempt
    unavailable,      // storage or crypto failed, or authentication did not start
};
// Why login_complete() returned no session: a wrong password, or the device could not check it or
// open a session (out of memory, no entropy, session table full).
enum class LoginFailure { none, wrong_password, unavailable };
// The HTTP status line and message for an enrollment result; `retry_after` asks for a Retry-After
// header (seconds, 0 for none).
struct EnrollReply {
    const char *status;
    const char *message;
    int retry_after;
};
EnrollReply enrollment_reply(EnrollResult result);
} // namespace monitor
