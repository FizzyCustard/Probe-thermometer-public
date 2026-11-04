// Host tests for the firmware's network guards and thermistor maths.
// Run: npm run test:native
#include <math.h>
#include <stdio.h>
#include "net_guards.h"
#include "thermistor.h"
#include "deferred_queue.h"

Adafruit_ADS1115 ads;
bool adsReady = false;

static int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) { printf("FAIL  %s\n", #cond); failures++; }      \
    else         { printf("PASS  %s\n", #cond); }                  \
  } while (0)

int main() {
  // Every spelling LittleFS resolves to the credential store
  CHECK(isBlockedStaticUrl("/wifi.json"));
  CHECK(isBlockedStaticUrl("//wifi.json"));
  CHECK(isBlockedStaticUrl("/./wifi.json"));
  CHECK(isBlockedStaticUrl("/index.html/../wifi.json"));
  CHECK(!isBlockedStaticUrl("/"));
  CHECK(!isBlockedStaticUrl("/app.js"));
  CHECK(!isBlockedStaticUrl("/chart.umd.min.js"));

  CHECK(originMatchesHost(nullptr, "bbq.local"));
  CHECK(originMatchesHost("", "bbq.local"));
  CHECK(originMatchesHost("http://bbq.local", "bbq.local"));
  CHECK(originMatchesHost("http://BBQ.local", "bbq.local"));
  CHECK(originMatchesHost("http://192.168.0.5", "192.168.0.5"));
  CHECK(originMatchesHost("http://localhost:8080", "localhost:8080"));
  CHECK(!originMatchesHost("http://evil.example", "bbq.local"));
  CHECK(!originMatchesHost("https://bbq.local.evil.example", "bbq.local"));
  CHECK(!originMatchesHost("http://bbq.local:8080", "bbq.local"));
  CHECK(!originMatchesHost("null", "bbq.local"));

  CHECK(isSafeFavId("f-1791234567890"));
  CHECK(!isSafeFavId(""));
  CHECK(!isSafeFavId("a,b"));
  CHECK(!isSafeFavId("a\nb"));
  CHECK(!isSafeFavId("x');alert(1);//"));
  CHECK(isSafeFavId("12345678901234567890123"));    // 23 chars fit
  CHECK(!isSafeFavId("123456789012345678901234"));  // 24 would be truncated

  CHECK(targetsValid(74, 160, 200));
  CHECK(!targetsValid(0, 0, 0));
  CHECK(!targetsValid(151, 160, 200));
  CHECK(!targetsValid(74, 200, 160));
  CHECK(!targetsValid(74, 160, 401));
  CHECK(!targetsValid(NAN, 160, 200));
  CHECK(!targetsValid(74, NAN, 200));
  CHECK(!targetsValid(74, 160, NAN));

  uint16_t taken = 0;
  int kept = 0;
  for (int i = 0; i < 500; i++) if (takeNoteSlot(taken, 100)) kept++;
  CHECK(kept == 100);

  CHECK(strcmp(fileBasename("/BBQ/cooks/cook_a.csv"), "cook_a.csv") == 0);
  CHECK(strcmp(fileBasename("cook_a.csv"), "cook_a.csv") == 0);

  // Default coefficients give a sane reading for a sane resistance
  float c = resistanceToCelsius(100000.0f, 0);
  CHECK(isfinite(c) && c > -50 && c < 120);
  CHECK(isnan(resistanceToCelsius(-1.0f, 0)));
  CHECK(isnan(resistanceToCelsius(1000.0f, 2)));

  // Coefficients that pass the old invT > 0 test but give nonsense temperatures
  probeCoeffs[0] = { 1e-4f, 0.0f, 0.0f };          // 10000 K
  CHECK(isnan(resistanceToCelsius(100000.0f, 0)));
  probeCoeffs[0] = { 1.0f, 0.0f, 0.0f };           // 1 K
  CHECK(isnan(resistanceToCelsius(100000.0f, 0)));
  probeCoeffs[0] = { INFINITY, 0.0f, 0.0f };
  CHECK(isnan(resistanceToCelsius(100000.0f, 0)));

  // Deferred writes keep their order and refuse, rather than overwrite, when full
  DeferredQueue<int, 3> q;
  int v = 0;
  CHECK(!q.pop(v));
  CHECK(q.push(1) && q.push(2) && q.push(3));
  CHECK(!q.push(4));
  CHECK(q.pop(v) && v == 1);
  CHECK(q.push(4));                       // wraps around the ring
  CHECK(q.pop(v) && v == 2);
  CHECK(q.pop(v) && v == 3);
  CHECK(q.pop(v) && v == 4);
  CHECK(!q.pop(v));

  printf("%s (%d failed)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
