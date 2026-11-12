# Wrong Wi-Fi network from Settings, 2026-11-12 (R03-08)

Physical ESP32-S3 on its saved home network, firmware `fe61e18`. Command (USB bridge; the output can
name the saved network, so it is not reproduced here):

```sh
python tools/device_wrong_wifi_test.py
```

The owner submitted `r03-08-no-such-network` through `POST /api/v1/wifi/connect` (answer `202`),
then the Wi-Fi status was read every 10 s for 12 minutes and the console's LCD view changes were
recorded.

| Seconds after submit | Wi-Fi state | Setup AP open | Attempt | Saved network kept |
| ---: | --- | --- | --- | --- |
| 1 | connected | yes | testing | yes |
| 11-22 | setup (trying the candidate) | yes | testing | yes |
| 32 | connected (back on the saved network) | yes | failed | yes |
| 32-623 | connected | yes | failed | yes |
| 634-718 | connected | **no** (the console logged station-only mode at 634 s) | failed | yes |

- The failed candidate left the saved network in place and the device rejoined it within about
  30 s.
- The setup access point closed on its own about 10 minutes after the failure, as designed.
- While the access point was open the LCD kept returning to the readings: the console logged the
  Overview and Trends views 47 times between 22 s and 718 s (the setup page between them is not
  logged). The physical panel was not watched.
- The R03-08 sub-check for the owner code no longer applies: the owner code was removed (R10-00).
