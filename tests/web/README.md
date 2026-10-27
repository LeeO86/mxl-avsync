# Web form regressions

The shipped Vue methods are exercised without external dependencies:

```sh
node --test tests/web/drafts.test.cjs
```

The real-browser regression starts its own service with a temporary state directory and port, runs through at least three one-second refreshes while typing, and checks Apply persistence, in-flight edits and Discard. It uses a separately installed Playwright package and Chromium, plus an existing built service. It does not connect to a production or lab instance:

```sh
BIN="$PWD/build/mxl-avsync" \
PLAYWRIGHT_MODULE=/path/to/playwright-core \
CHROMIUM_PATH=/path/to/chromium \
node tests/web/settings.cjs
```

Alternatively use the `playwright` package with its installed Chromium and omit the two browser overrides. Keep browser packages outside the repository when using the override path. The C++ API integration script `tests/integration/settings.sh` separately checks validation, persistence, environment precedence, restart-required keys and import/export.
