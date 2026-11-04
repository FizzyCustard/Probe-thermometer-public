# Security policy

## Reporting a problem

Please don't open a public issue for a security problem. Report it privately
through GitHub instead: open the repository's **Security** tab and choose
**Report a vulnerability**. You should hear back within a couple of weeks. This
is a hobby project maintained in spare time.

## What's in scope

The firmware in `arduino/bbq-thermometer/` and the web UI in `device/data/`.

## Known limits, by design

The device is built for a trusted home network. These are documented in the
[README](README.md#security) and are not treated as vulnerabilities:

- The dashboard, JSON API and WebSocket have no login. Anyone on your network
  can read temperatures and change settings, including WiFi details.
- The WebSocket origin check does not stop DNS rebinding attacks.
- Traffic is plain HTTP.

Reports that bypass `OTA_PASSWORD`, read the stored WiFi password, crash or
lock up the device remotely, or let another website control it beyond the
limits above are all welcome.
