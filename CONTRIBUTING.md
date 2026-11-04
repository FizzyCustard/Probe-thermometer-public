# Contributing

Thanks for taking a look. Bug reports, fixes and build write-ups are all
welcome.

## Before you start

For anything bigger than a small fix, open an issue first so we can agree on
the approach. Planned features are in [`ROADMAP.md`](ROADMAP.md).

## Building

Follow [Build your own](README.md#build-your-own) in the README. In short:

```bash
cp arduino/bbq-thermometer/config.example.h arduino/bbq-thermometer/config.h
# edit config.h and replace both CHANGE_ME passwords
pio run
```

Never commit `config.h`. It holds your passwords and is gitignored.

## Tests

```bash
npm install
npx playwright-core install chromium-headless-shell
npm test
```

CI runs the same tests and a firmware build on every pull request. If you
change firmware behaviour, please say in the pull request whether you tested it
on real hardware.

## Firmware rules

- Never block inside the async WebSocket or HTTP callbacks. That means no SD or
  flash access and no WiFi scans. Queue the work and do it from `wsTick()` in
  `loop()` instead. See [`arduino/FIRMWARE-SPEC.md`](arduino/FIRMWARE-SPEC.md).
- The WebSocket payload in `websocket_handler.h` and the parser in
  `device/data/app.js` change together. Update both in the same pull request.
- RAM is tight on the ESP8266. Prefer fixed-size buffers to growing `String`s.

## Licence

By contributing you agree that your work is released under the
[MIT License](LICENSE).
