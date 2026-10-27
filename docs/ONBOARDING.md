# First-run and Wi-Fi onboarding

## First owner setup

With no saved station network, the device starts its WPA2 setup access point.
The SSID is `TempMonitor-` followed by the last three Wi-Fi MAC bytes in
uppercase hex. The AP password is a random per-device value saved in NVS; there
is no factory-wide password. Retrieve the AP password and one-time administrator
setup secret over the physical USB console with:

```sh
python tools/device_setup.py --port /dev/ttyACM0
```

The utility saves the response in `.build/device-setup.json` with mode 0600 and
does not print credential values. Keep that file private. Join the named AP,
then open `http://192.168.4.1` if the phone does not open its captive page. Use
the one-time setup secret to enroll the administrator password. Enrollment
disables the bootstrap secret for future network use; an ordinary restart or
Wi-Fi recovery does not restore it.

After enrollment, use **Settings → Network** to scan visible 2.4 GHz networks or
enter a hidden SSID manually. Submit the Wi-Fi password once. The device tests
the candidate before saving it. The setup AP remains available during the test;
if association/DHCP fails, the previous credentials remain and the UI offers a
retry. On success, the device reports its DHCP address and hostname. The AP is
closed after the handoff window. A station network is not required for local
setup or temperature monitoring.

## Reopen setup

Hold the GPIO0 **BOOT** button for five seconds after startup to open the setup
AP for a temporary ten-minute window. This operation does not erase saved
Wi-Fi credentials, administrator enrollment, sensor settings, or history. A
new candidate replaces the previous station credentials only after successful
connection and DHCP. This is distinct from physical USB administrator
credential recovery and from a future full factory-reset operation.

The BOOT hold threshold and Wi-Fi handoff are implemented and covered by host
logic/API checks. The physical button hold, AP visibility from a phone, and
phone captive-portal experience have not passed hardware acceptance; see
[TODO.md](TODO.md) (`T05-13`, `T05-18`).

## Local network security

The owner selected local HTTP and accepted its confidentiality limitation.
The setup password protects the AP association, and app actions use sessions,
CSRF tokens, and Origin/Host checks. HTTP still does not encrypt the administrator
password, Wi-Fi password, or session from other devices on a shared LAN. Do not
use WAN port forwarding. The HTTPS certificate/trust design is documented as a
future option in [SECURITY_DESIGN.md](SECURITY_DESIGN.md).
