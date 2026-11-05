# Proposed administration and enrollment design

Status: local HTTP selected by the owner on 2026-10-25, with explicit acceptance
of LAN credential/session exposure. Core authentication is implemented and tested; remaining release gates are open.
The HTTPS option below remains a future alternative.

## Preferred option: HTTPS

Serve the app, REST, WebSocket upgrades and OTA over HTTPS/WSS. Provision a
unique device certificate and key into a separate identity partition, outside
OTA slots. Keep private keys out of source control, downloadable firmware and
logs. The browser must explicitly trust the device certificate (or a local
issuer); expose its public fingerprint through physical USB and the LCD.
Certificate replacement is an explicit owner operation, not part of ordinary
OTA. The setup AP remains WPA2 protected with a random per-device password.
A narrowly scoped HTTP captive landing page may redirect to HTTPS and serve
public setup instructions, but must not accept administrative credentials.

This protects credentials on the LAN after certificate trust is established.
The tradeoff is a certificate trust step on each browser/device. Automated
clients pin the public certificate/issuer instead of disabling verification.

## Selected option: local HTTP (explicitly accepted)

Keep the same device-unique setup credentials, enrollment, password verifier,
CSRF checks and expiring sessions, but serve HTTP/WS on the LAN. Shared-LAN
observers can capture passwords, sessions and Wi-Fi credential submissions.
Sessions do not provide confidentiality. This option requires explicit owner
acceptance under SPEC section 12 and cannot be treated as equivalent to HTTPS.

## Public read model (decided 2026-11-05)

The owner chose that anyone who can reach the device may see readings and
history; only changing things needs the password.

- **Public (no sign-in):** current readings and the live stream, history and
  CSV export, alert history, sensor names and probe ROM IDs, probe-scan and
  clock status, and the display settings (units, timezone, display mode,
  dwell times, lookback). On the LAN that means anyone on the network; on the
  setup access point it means anyone who has joined it.
- **Owner only:** every change (sensor and device settings, retiring a probe),
  Wi-Fi status, scans and credentials, firmware upload, restart, factory reset
  and password change. These need a session, and changes also need the CSRF
  token. The NTP server setting is owner-only.
- **Never public:** the setup Wi-Fi key and owner code (LCD and USB only),
  session tokens, the Wi-Fi SSID and IP details.
- **One access table:** `firmware/components/monitor_core/route_access.cpp`
  decides which routes are public or owner-only and is covered by a host test;
  a route that is not listed is a 404 for everyone.
- **Load protection:** public history is limited to one query per second and one
  CSV export per 30 seconds per client address, with `429` and `Retry-After`
  beyond that. The live stream admits six clients and keeps one slot for a
  signed-in owner, so a crowd of viewers cannot lock the owner out. The upgrade
  still requires the device's own `Origin`, which stops another website's page
  from reading the stream in a visitor's browser.
- **Accepted limitation:** traffic is plain HTTP. Anyone on the network can read
  the public data, and the public data includes probe ROM IDs and the names you
  give your sensors. That is the owner's choice.

## Enrollment and recovery (common behavior)

- First owner proves access with a random one-time setup secret delivered
  through the LCD or a physical USB provisioning utility. No universal default
  administrator password; do not derive secrets solely from the MAC address.
- Enroll a new administrator password; store a random salt and a deliberately
  costed PBKDF2-HMAC-SHA256 verifier using vetted crypto. Measure KDF latency on
  the actual S3 and confirm acquisition/watchdog behavior during login.
- Persist an enrollment marker that disables the initial bootstrap secret for
  network use. Ordinary reboot, Wi-Fi loss or BOOT recovery must not re-enable it.
- Use bounded RAM sessions with random tokens, expiration, HttpOnly/SameSite
  cookies (Secure under HTTPS), and separate CSRF tokens. Never persist passwords
  or session tokens in browser localStorage.
- Validate Host and Origin, bound payloads and client counts, rate-limit
  authentication, and require the owner for every change (see the public read
  model below), Wi-Fi details and OTA.
- Five-second BOOT opens a temporary protected Wi-Fi provisioning window only;
  retain existing Wi-Fi settings until a candidate succeeds. It does not reset
  administrator credentials or erase history.
- Forgotten administrator credentials require physical USB recovery with an
  explicit credential-reset operation. Preserve sensor settings/history; a full
  factory reset is a separate confirmed operation.
- Default physical threat model: possession of the board/USB permits recovery.
  Flash encryption/secure boot require a separate, deliberate provisioning plan;
  do not burn irreversible eFuses during ordinary automated device tests.

The implementation must test the selected flow, including rejected bootstrap
reuse, login rate limits, session expiry, forged origins, unauthenticated Wi-Fi
changes, socket upgrades, exports, malformed OTA uploads, and that each public
and owner route behaves as the table above says, before release. OTA uploads
are checked to be for this product (`project_name`) before anything is written;
images are not signed and secure boot is out of scope.

## Development implementation evidence

The current firmware implements HTTP/SameSite/HttpOnly sessions, a random per-device WPA2 setup
key, a random one-time owner code, and a PBKDF2-HMAC-SHA256 password verifier (100,000 iterations,
128-bit random salt). A known-answer check of the key derivation runs at startup and fails closed
(authentication unavailable) rather than crashing.

**Setup secrets are short on purpose, because they are typed by hand from the LCD:**

- **Wi-Fi setup key:** 12 characters from a 32-symbol alphabet without `0`, `1`, `I` and `O`:
  60 bits. It protects the setup access point, which is only open for a limited time after a BOOT
  hold (and for ten minutes after a failed Wi-Fi attempt), not permanently. Anyone who captures a
  join handshake while the access point is open could try keys offline; 60 random bits make that
  expensive, not impossible for a determined attacker with large resources, which is why the window
  is short.
- **Owner code:** 16 characters from the same alphabet: 80 bits, one time, shown on the LCD and
  over USB. What is typed is normalised (case, spaces and dashes are ignored); a malformed entry is
  reported as such and is not counted as a guess, a wrong one starts a five-second spacing before
  the next attempt, and enrollment only works while the setup window is open. Once an owner exists
  the code is erased. An older 256-bit code still stored on a device keeps working exactly as
  typed until the next start replaces it with the shorter format.
- **Why not longer:** a 32-character code cannot realistically be read from a 1.8-inch screen and
  typed on a phone. The limits above (physical BOOT hold, closing window, spacing between guesses,
  one-time use) are what protect it.

**Login:** a wrong password makes that client wait 5, 10, 20, 40 and then 60 seconds, per client
address, so one address cannot lock the owner out from another; only one password check runs at a
time and a refused attempt answers `429` with `Retry-After`. The password check is deliberately slow:
about 14.7 seconds was measured on the 160 MHz ESP32-S3 earlier, and a login on 2026-11-05 took 30.7
seconds, so the sign-in screen shows a waiting state and the check runs on its own task so sampling
and the other pages keep working. Passwords are 12 to 128 characters.

Physical recovery requires the explicit USB command
`RESET_ADMIN_CREDENTIALS CONFIRM`. It removes only administrator verifier/bootstrap
keys and restarts; Wi-Fi, sensor settings and history remain separate. It has no
LAN endpoint. Automated fresh-enrollment tests invoke it only for an account
marked as created by the device test runner. The USB HTTP bridge is a physical
owner/testing interface, not an unauthenticated LAN route.

Development credentials are kept in ignored, mode-0600 files under `.build/`.
Do not attach those files or raw bridge responses to issues or test reports.
