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
- Validate Host and Origin, bound payloads/client queues, rate-limit authentication,
  and authenticate sensitive reads, all mutations, WebSocket upgrades and OTA.
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
changes, socket upgrades, exports and malformed OTA uploads before release.

## Development implementation evidence

The current firmware implements HTTP/SameSite/HttpOnly sessions, a unique WPA2
setup password, a random 256-bit one-time bootstrap secret, and PBKDF2-HMAC-SHA256
with 100,000 iterations and a 128-bit random salt. A known-answer check runs at
startup. The measured KDF cost on the current 160 MHz S3 is approximately 14.7–14.8
seconds; login/enrollment show a waiting state. Failed login cooldown begins
**after** hashing completes and increases up to 60 seconds, so expensive failures
cannot continuously occupy the HTTP task. Sampling runs separately.

Physical recovery requires the explicit USB command
`RESET_ADMIN_CREDENTIALS CONFIRM`. It removes only administrator verifier/bootstrap
keys and restarts; Wi-Fi, sensor settings and history remain separate. It has no
LAN endpoint. Automated fresh-enrollment tests invoke it only for an account
marked as created by the device test runner. The USB HTTP bridge is a physical
owner/testing interface, not an unauthenticated LAN route.

Development credentials are kept in ignored, mode-0600 files under `.build/`.
Do not attach those files or raw bridge responses to issues or test reports.
