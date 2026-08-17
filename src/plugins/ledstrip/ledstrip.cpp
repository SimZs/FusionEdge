#include "../../core/options.h"
#include "ledstrip.h"
#include <Adafruit_NeoPixel.h>

#include "../../core/config.h"
#include "../../core/player.h"
#include "../../core/network.h"
#include "../../core/display.h"
#include "driver/gpio.h"



extern Player player;
extern Display display;

// -----------------------------------------------------------------------------
// VU provider hookok
// Ha később találsz stabil audio/VU forrást, csak implementáld ezeket máshol.
// 0..255 tartományt várunk.
// -----------------------------------------------------------------------------
extern "C" __attribute__((weak)) uint8_t fusion_led_vu_left()  { return 0; }
extern "C" __attribute__((weak)) uint8_t fusion_led_vu_right() { return 0; }

// -----------------------------------------------------------------------------

#ifndef LEDSTRIP_PIN
  #define LEDSTRIP_PIN 48
#endif

#ifndef LEDSTRIP_BRIGHTNESS
  #define LEDSTRIP_BRIGHTNESS 80
#endif

#define LEDSTRIP_VOL_TIMEOUT_MS   1800
#define LEDSTRIP_FLASH_MS          180
#define LEDSTRIP_FRAME_MS           18
#define LEDSTRIP_IDLE_PULSE_MS      22
#define LEDSTRIP_SCREEN_PULSE_MS    20   // gyorsabb tick, sin simítja
#define LEDSTRIP_CONNECT_PULSE_MS   26

// -----------------------------------------------------------------------------
// LED-szám-arányos skálázás
// Minden animáció-paramétert a "referencia" 144 LED-hez képest skálázunk.
// Ez biztosítja, hogy 12, 24, 60 LED-nél is ugyanolyan dinamikus legyen.
// -----------------------------------------------------------------------------
#define LED_REFERENCE_COUNT   144

// -----------------------------------------------------------------------------
// VU dinamikus tartomány + gamma leképezés
// -----------------------------------------------------------------------------
// Gamma táblázat: gamma = 2.0
// A VU forrás 50-230 körül ingadozik, ez a görbe ezt a tartományt
// szépen szétteríti 0..9 pixelre (12 LED-es félszalagnál).
// Értékek: round(255 * (i/255)^2.0) for i in 0..255
static const uint8_t vuGamma[256] PROGMEM = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,
    1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,
    4,   4,   5,   5,   5,   5,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,
    9,   9,  10,  10,  11,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,
   16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  23,  23,  24,  24,
   25,  26,  26,  27,  28,  28,  29,  30,  30,  31,  32,  32,  33,  34,  35,  35,
   36,  37,  38,  38,  39,  40,  41,  42,  42,  43,  44,  45,  46,  47,  47,  48,
   49,  50,  51,  52,  53,  54,  55,  56,  56,  57,  58,  59,  60,  61,  62,  63,
   64,  65,  66,  67,  68,  69,  70,  71,  73,  74,  75,  76,  77,  78,  79,  80,
   81,  82,  84,  85,  86,  87,  88,  89,  91,  92,  93,  94,  95,  97,  98,  99,
  100, 102, 103, 104, 105, 107, 108, 109, 111, 112, 113, 115, 116, 117, 119, 120,
  121, 123, 124, 126, 127, 128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143,
  145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 162, 163, 165, 166, 168,
  170, 171, 173, 175, 176, 178, 180, 181, 183, 185, 186, 188, 190, 192, 193, 195,
  197, 199, 200, 202, 204, 206, 207, 209, 211, 213, 215, 217, 218, 220, 222, 224,
  226, 228, 230, 232, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255
};



// vuMap: VU (0..255) → pixel-szám (0..maxLed), gamma korrekcióval
// Nincs normalizálás – a VU forrás már 0..255 skálán dolgozik
static uint16_t vuMap(uint8_t vu, uint16_t maxLed) {
  uint8_t gv = pgm_read_byte(&vuGamma[vu]);
  return (uint16_t)((uint32_t)gv * maxLed / 255U);
}

// Breathing sin-lépések száma egy félciklusban (fel vagy le)
// SCREEN_PULSE_MS * BREATH_STEPS = félciklus ideje ms-ben
// pl. 20ms * 80 = 1600ms fel, 1600ms le → ~3.2s egy teljes lélegzet
#define BREATH_STEPS  80

#ifndef LEDSTRIP_COUNT_MAX
  #define LEDSTRIP_COUNT_MAX 144
#endif

// Runtime LED szám a config-ból (max LEDSTRIP_COUNT_MAX)
// Forward declaration hogy a fájl elejéről is elérhető legyen
static Adafruit_NeoPixel strip(LEDSTRIP_COUNT_MAX, LEDSTRIP_PIN, NEO_GRB + NEO_KHZ800);

static inline bool ledStripPinAvailable() {
  return GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)LEDSTRIP_PIN);
}

// Runtime LED szám: config értéke, max LEDSTRIP_COUNT_MAX (nincs min() típuskonfliktus)
static inline uint16_t ledCount() {
    uint16_t n = config.store.lsCount;
    return (n > LEDSTRIP_COUNT_MAX) ? (uint16_t)LEDSTRIP_COUNT_MAX : (n < 1 ? (uint16_t)1 : n);
}
// Skáláz egy értéket: ref_val 144 LED-nél → arányos érték ledCount()-nál
static inline uint16_t ledScale(uint16_t ref_val, uint16_t min_val = 1) {
  uint32_t v = ((uint32_t)ref_val * ledCount() + LED_REFERENCE_COUNT / 2) / LED_REFERENCE_COUNT;
  return (uint16_t)(v < min_val ? min_val : v);
}

// Skáláz float értéket (decay, lépésköz stb.)
static inline float ledScaleF(float ref_val, float min_val = 0.0f) {
  float v = ref_val * ledCount() / (float)LED_REFERENCE_COUNT;
  return (v < min_val) ? min_val : v;
}

enum LedMode : uint8_t {
  LM_BOOT = 0,
  LM_CONNECTING,
  LM_STOP,
  LM_PLAY,
  LM_BUFFERING,
  LM_VOLUME,
  LM_SCREENSAVER
};

static LedMode   g_mode               = LM_BOOT;
static LedMode   g_lastMode           = (LedMode)0xFF;  // módváltás detektáláshoz (szándékosan érvénytelen, hogy az első renderX() resetelődjön)
static uint8_t   g_lastVolume         = 255;
static uint32_t  g_volumeUntil        = 0;
static uint32_t  g_flashUntil         = 0;
static uint32_t  g_lastFrame          = 0;
static uint32_t  g_lastPulse          = 0;
static uint16_t  g_rainbowIndex       = 0;
// sin-alapú breathing: 0..2*BREATH_STEPS-1 fázis
static uint16_t  g_breathPhase        = 0;
// régi pulse (connecting, buffering, boot)
static uint8_t   g_pulseBrightness    = 20;
static int8_t    g_pulseDir           = 1;
static uint8_t   g_peakL              = 0;
static uint8_t   g_peakR              = 0;
static uint8_t   g_flashR             = 0;
static uint8_t   g_flashG             = 0;
static uint8_t   g_flashB             = 0;
static bool      g_connectedSeen      = false;

// --- Knight Rider (connecting) ---
static int16_t   g_krPos             = 0;      // aktuális fej-pozíció
static int8_t    g_krDir             = 1;      // +1 jobbra, -1 balra
static uint32_t  g_krLastFrame       = 0;

// --- Rainbow flow (model=1) ---
static uint32_t  g_rainbowLastFrame   = 0;

// --- Sparkle/Twinkle (model=2) ---
// Minden pixel saját fényerő-állapotot tárol (0=sötét, >0=aktív/fading)
#define SPARKLE_FADE_STEPS  20          // hány frame alatt hal el egy szikra
static uint8_t   g_sparkleBr[LEDSTRIP_COUNT_MAX];   // 0..SPARKLE_FADE_STEPS
static uint8_t   g_sparkleHue[LEDSTRIP_COUNT_MAX];  // hue index (0..255)
static uint32_t  g_sparkleLastFrame  = 0;
// Simított sávhossz – középről kifelé, mint a rainbow-nál
static float     g_sparkleLen        = 0.0f;  // 0.0 .. half
// --- Meter VU (model=3) ---
// Fehér sáv + piros csúcsmutató, L bal / R jobb, ~0.5mp visszaesés
static float     g_meterPeakL        = 0.0f;
static float     g_meterPeakR        = 0.0f;

// sin-tábla 0..BREATH_STEPS-1 → 0..255  (negyed periódus, szimmetrikusan tükrözve)
// Előre számolt, hogy ESP32-n ne kelljen float sin() minden frame-ben
static const uint8_t sinTable[81] PROGMEM = {
    0,  3,  6, 10, 13, 16, 19, 22, 25, 28,
   31, 34, 37, 40, 43, 46, 49, 52, 55, 58,
   60, 63, 66, 68, 71, 73, 76, 78, 80, 83,
   85, 87, 89, 91, 93, 95, 97, 99,100,102,
  104,105,107,108,110,111,112,113,115,116,
  117,118,119,119,120,121,121,122,122,123,
  123,124,124,124,124,125,125,125,125,125,
  125,125,125,124,124,124,124,123,123,122,
  122
};

// -----------------------------------------------------------------------------
// helper
// -----------------------------------------------------------------------------

static inline uint8_t clamp8(int v) {
  if (v < 0)   return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return strip.Color(r, g, b);
}

static void clearStrip() {
  strip.clear();
}

static void showStrip() {
  strip.show();
}

static void fillAll(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < ledCount(); i++) {
    strip.setPixelColor(i, rgb(r, g, b));
  }
}

// VU szín: cián→zöld→sárga→narancs→piros spektrum (180°, jól látható, kevés kék)
// idx=0 (közép/csend) → cián/zöld, idx=total-1 (max) → piros
// Adafruit hue: 0=piros, 10922=sárga, 21845=zöld, 32768=cián
static uint32_t vuHsvColor(uint16_t idx, uint16_t total, uint8_t brightness) {
  if (total == 0) return rgb(0, 0, 0);
  uint16_t n = (total > 1) ? total - 1 : 1;
  // cián(32768) → piros(0): csökkenő hue – szép zöld-sárga-piros ív
  uint32_t hue = 32768UL - ((uint32_t)idx * 32768UL / n);
  // Telítettség: középen kicsit pasztell (200), külsőn teli (255)
  uint8_t sat = (uint8_t)(200 + (uint16_t)(idx * 55) / n);
  return strip.gamma32(strip.ColorHSV((uint16_t)hue, sat, brightness));
}

// Exponenciális envelope follower a LED strip VU-hoz.
// A getVUlevel() "peak" értékeket ad vissza (attack=azonnali, release=lineáris per-sample),
// ami frame-szinten nagyon ugrálósan jelenik meg. Az alábbi follower kisimítja:
//
//   ATTACK_MS  ~25ms  – gyors követés felfelé  (hallható ütés megjelenik)
//   RELEASE_MS ~220ms – lassú esés lefelé      (nincs idegesen ugráló visszaesés)
//
// A koefficiensek az SA analógiájára számolva:
//   coef = 1 - exp(-dt / tau)   ahol dt = LEDSTRIP_FRAME_MS / 1000.0
//
// Ezek konstans értékek a fordítási időben is kiszámolhatók, de az ESP32-n
// a setup()-ban egyszer lefutó expf() is teljesen rendben van.
//
// Megjegyzés: a getVUlevel() left_peak / right_peak csatornái könnyen elérik
// a 200-255 tartományt erős jelnél, de csendben ~0-ra esnek. A smoother
// megakadályozza az abrupt 0-ra ugrást és az egymás utáni frame-ek közötti
// nagy amplitúdó-különbséget.

static float    g_smoothLf    = 0.0f;
static float    g_smoothRf    = 0.0f;
static uint8_t  g_smoothL     = 0;
static uint8_t  g_smoothR     = 0;

// Koefficiensek: frame = 18ms
// attack  tau = 25ms  → coef = 1 - exp(-0.018/0.025) ≈ 0.514
// release tau = 220ms → coef = 1 - exp(-0.018/0.220) ≈ 0.079
static float g_vuAttCoef  = 0.514f;
static float g_vuRelCoef  = 0.079f;

static void vuSmoothInit() {
  // Kiszámoljuk a koefficienseket az aktuális LEDSTRIP_FRAME_MS alapján
  // (ezt setupban hívjuk egyszer)
  const float dt = LEDSTRIP_FRAME_MS / 1000.0f;
  g_vuAttCoef = 1.0f - expf(-dt / 0.025f);   // 25ms attack
  g_vuRelCoef = 1.0f - expf(-dt / 0.220f);   // 220ms release
}

static void updateSmoothedVU(uint8_t rawL, uint8_t rawR) {
  float fl = rawL / 255.0f;
  float fr = rawR / 255.0f;

  // Exponenciális envelope follower
  float coefL = (fl > g_smoothLf) ? g_vuAttCoef : g_vuRelCoef;
  float coefR = (fr > g_smoothRf) ? g_vuAttCoef : g_vuRelCoef;

  g_smoothLf += coefL * (fl - g_smoothLf);
  g_smoothRf += coefR * (fr - g_smoothRf);

  // Clamp + konverzió uint8_t-be (0..255)
  if (g_smoothLf < 0.0f) g_smoothLf = 0.0f;
  if (g_smoothLf > 1.0f) g_smoothLf = 1.0f;
  if (g_smoothRf < 0.0f) g_smoothRf = 0.0f;
  if (g_smoothRf > 1.0f) g_smoothRf = 1.0f;

  g_smoothL = (uint8_t)(g_smoothLf * 255.0f);
  g_smoothR = (uint8_t)(g_smoothRf * 255.0f);
}


static void decayPeaks() {
  // Csúcs visszaesési sebesség: arányos a LED-számhoz.
  // 144 LED-nél 1px/frame gyors, 12 LED-nél 1px/frame a teljes skálán átér 12 frame alatt.
  // Megoldás: frame-ek felosztása – kis számnál ritkábban esik a csúcs.
  static uint8_t s_decayCounter = 0;
  // decay intervallum: referencia / ledCount() (kerekítve, min 1)
  uint8_t interval = (uint8_t)(LED_REFERENCE_COUNT / ledCount());
  if (interval < 1) interval = 1;
  s_decayCounter++;
  if (s_decayCounter >= interval) {
    s_decayCounter = 0;
    if (g_peakL > 0) g_peakL--;
    if (g_peakR > 0) g_peakR--;
  }
}

static void renderStereoVU(uint8_t vuL, uint8_t vuR) {
  const uint16_t half       = ledCount() / 2;
  const uint16_t leftCount  = half;
  const uint16_t rightCount = ledCount() - half;

  uint16_t litL = vuMap(g_smoothL, leftCount);
  uint16_t litR = vuMap(g_smoothR, rightCount);

  if (litL > g_peakL) g_peakL = litL;
  if (litR > g_peakR) g_peakR = litR;

  clearStrip();

  // Bal: középtől kifelé (i=0 közel/csend, i=leftCount-1 külső/max)
  for (uint16_t i = 0; i < leftCount; i++) {
    uint16_t rev = leftCount - 1 - i;
    if (i < litL) {
      // Fényesebb kifelé: 160 → 240
      uint8_t br = (uint8_t)(160 + (uint16_t)(i * 80) / (leftCount > 1 ? leftCount - 1 : 1));
      strip.setPixelColor(rev, vuHsvColor(i, leftCount, br));
    } else if (i == litL && litL > 0) {
      // 1. fade pixel az él után: ~40% fényerő
      strip.setPixelColor(rev, vuHsvColor(i, leftCount, 70));
    } else if (i == litL + 1 && litL > 0) {
      // 2. fade pixel: ~15% – puha lecsengés
      strip.setPixelColor(rev, vuHsvColor(i, leftCount, 28));
    }
  }

  // Jobb: középtől kifelé
  for (uint16_t i = 0; i < rightCount; i++) {
    uint16_t idx = half + i;
    if (i < litR) {
      uint8_t br = (uint8_t)(160 + (uint16_t)(i * 80) / (rightCount > 1 ? rightCount - 1 : 1));
      strip.setPixelColor(idx, vuHsvColor(i, rightCount, br));
    } else if (i == litR && litR > 0) {
      strip.setPixelColor(idx, vuHsvColor(i, rightCount, 70));
    } else if (i == litR + 1 && litR > 0) {
      strip.setPixelColor(idx, vuHsvColor(i, rightCount, 28));
    }
  }

  // Peak marker: mindig megjelenik amíg van jel (a bar csúcsán is)
  if (g_peakL > 0 && g_peakL <= leftCount) {
    uint16_t pidx = leftCount - g_peakL;
    strip.setPixelColor(pidx, rgb(255, 255, 255));
    if (pidx + 1 < leftCount)
      strip.setPixelColor(pidx + 1, rgb(50, 50, 50));
  }
  if (g_peakR > 0 && g_peakR <= rightCount) {
    uint16_t pidx = half + (g_peakR - 1);
    strip.setPixelColor(pidx, rgb(255, 255, 255));
    if (pidx > half)
      strip.setPixelColor(pidx - 1, rgb(50, 50, 50));
  }

  showStrip();
  decayPeaks();
}

static void flashNow(uint8_t r, uint8_t g, uint8_t b, uint16_t ms = LEDSTRIP_FLASH_MS) {
  g_flashR = r;
  g_flashG = g;
  g_flashB = b;
  g_flashUntil = millis() + ms;
}

static bool isScreensaverMode() {

    if (config.isScreensaver)
        return true;

    if (display.mode() == SCREENSAVER)
        return true;

    if (display.mode() == SCREENBLANK)
        return true;

    return false;
}

static void renderFlash() {
  fillAll(g_flashR, g_flashG, g_flashB);
  showStrip();
}


// Sima sin-breathing egyszínű töltéssel (connecting, buffering, boot)
static void renderPulse(uint8_t r, uint8_t g, uint8_t b, uint8_t minBr, uint8_t maxBr, uint8_t step, uint16_t speedMs) {
  // Módváltáskor reset
  if (g_mode != g_lastMode) {
    g_pulseBrightness = minBr;
    g_pulseDir        = 1;
    g_lastMode        = g_mode;
  }

  if (millis() - g_lastPulse < speedMs) return;
  g_lastPulse = millis();

  g_pulseBrightness += g_pulseDir * step;
  if (g_pulseBrightness >= maxBr) { g_pulseBrightness = maxBr; g_pulseDir = -1; }
  if (g_pulseBrightness <= minBr) { g_pulseBrightness = minBr; g_pulseDir =  1; }

  uint8_t rr = (uint8_t)((r * g_pulseBrightness) / 255U);
  uint8_t gg = (uint8_t)((g * g_pulseBrightness) / 255U);
  uint8_t bb = (uint8_t)((b * g_pulseBrightness) / 255U);
  fillAll(rr, gg, bb);
  showStrip();
}

// Sin-alapú, lélegzetenként más szín (fix 16 szín, egyenletesen elosztva)
static uint32_t  g_lastBreath  = 0;
static uint8_t   g_ssColorIdx  = 0;   // 0..15, lélegzetenként lép

static void renderSinBreath() {
  if (g_mode != g_lastMode) {
    g_breathPhase = 0;
    g_ssColorIdx  = 0;
    g_lastMode    = g_mode;
  }

  if (millis() - g_lastBreath < LEDSTRIP_SCREEN_PULSE_MS) return;
  g_lastBreath = millis();

  uint16_t p = g_breathPhase;
  uint8_t  s;
  if (p < BREATH_STEPS) {
    s = pgm_read_byte(&sinTable[p]);
  } else {
    uint16_t mirror = (2 * BREATH_STEPS - 1) - p;
    s = pgm_read_byte(&sinTable[mirror]);
  }
  // Mélyebb sötétedés: min=3 (majdnem ki), max=203 → nagy kontraszt
  // A felkúszás is simított (sinTable már eleve sin-görbét követ → nincs hirtelen ugrás)
  uint8_t br = 3 + (uint8_t)((uint16_t)s * 200U / 122U);

  g_breathPhase++;
  if (g_breathPhase >= 2 * BREATH_STEPS) {
    g_breathPhase = 0;
    g_ssColorIdx  = (g_ssColorIdx + 1) & 0x0F;  // 0..15
  }

  // 16 egyenletes hue lépés a teljes körön
  uint16_t hue = (uint16_t)g_ssColorIdx * (65536U / 16);
  uint32_t c = strip.ColorHSV(hue, 255, br);
  for (uint16_t i = 0; i < ledCount(); i++) {
    strip.setPixelColor(i, c);
  }
  showStrip();
}

static void renderRainbow() {
  for (uint16_t i = 0; i < ledCount(); i++) {
    uint16_t hue = g_rainbowIndex + (i * (65536UL / ledCount()));
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
  }
  showStrip();
  // Referencia: 220 lépés/frame @ 144 LED → skálázva arányos forgássebesség
  // Kis számnál (12 LED) a lépés ~24× nagyobb lenne → fix referencia-sebességet tartunk
  g_rainbowIndex += 220;  // szándékosan NEM skálázott: a forgás "szögsebessége" legyen állandó
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Rainbow flow (model=1)
// - Szivárvány középről kifelé szimmetrikusan
// - VU lökés: a megjelenített sáv hossza a VU alapján változik (többi fekete)
//             + erős hang hirtelen hue ugrást ad (ritmus érzet)
// - VU nélkül: teljes szalag, lassú forgás
// -----------------------------------------------------------------------------
// Simított sávhossz tracker: exponenciálisan közelít a célhoz (nem bináris kapcsoló)
static float g_rbFlowLen = 0.0f;

static void renderRainbowFlow() {
  const uint32_t FLOW_MS = 20;
  if (millis() - g_rainbowLastFrame < FLOW_MS) return;
  g_rainbowLastFrame = millis();

  uint8_t vuAvg = (uint8_t)(((uint16_t)g_smoothL + g_smoothR) >> 1);
  const uint16_t half = ledCount() / 2;

  // Célhossz: csendben teljes szalag (lassú forgás), jelnél VU-arányos
  float targetLen;
  if (vuAvg < 5) {
    targetLen = (float)half;   // csend: teljes szalag, halvány
  } else {
    uint16_t litMin = ledScale(4, 1);
    targetLen = (float)(litMin + vuMap(vuAvg, half - litMin));
  }

  // Exponenciális simítás: gyors felfelé (~3 frame), lassú lefelé (~15 frame)
  float lenCoef = (targetLen > g_rbFlowLen) ? 0.35f : 0.07f;
  g_rbFlowLen += lenCoef * (targetLen - g_rbFlowLen);
  if (g_rbFlowLen < 1.0f) g_rbFlowLen = 1.0f;
  if (g_rbFlowLen > (float)half) g_rbFlowLen = (float)half;

  uint16_t litHalf = (uint16_t)(g_rbFlowLen + 0.5f);
  if (litHalf < 1) litHalf = 1;

  // Fényerő és forgássebesség: csendben halvány+lassú, jelnél élénk+gyors
  uint8_t  br       = (vuAvg < 5) ? 140 : 220;
  uint16_t rotSpeed = (vuAvg < 5)
    ? (uint16_t)((uint32_t)120 * LED_REFERENCE_COUNT / ledCount())
    : (uint16_t)((uint32_t)380 * LED_REFERENCE_COUNT / ledCount());

  // Erős ütem → extra hue lökés (ritmuskövetés)
  if (vuAvg > 170) {
    uint16_t jump = (uint16_t)((uint32_t)(vuAvg - 170) * 1000U / 85U);
    g_rainbowIndex += (uint16_t)((uint32_t)jump * LED_REFERENCE_COUNT / ledCount());
  }
  g_rainbowIndex += rotSpeed;

  clearStrip();
  for (uint16_t i = 0; i < litHalf; i++) {
    uint16_t hue = g_rainbowIndex + (uint32_t)i * 65536UL / litHalf;
    uint32_t col = strip.gamma32(strip.ColorHSV(hue, 255, br));
    strip.setPixelColor(half - 1 - i, col);
    strip.setPixelColor(half + i,     col);
  }
  showStrip();
}
// qadd8 helper (saturating add, ha nincs FastLED)
static inline uint8_t qadd8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + b;
  return s > 255 ? 255 : (uint8_t)s;
}

// -----------------------------------------------------------------------------
// Sparkle / Twinkle (model=2)
// Random pixelek villannak fel és halnak el fokozatosan (fade-out).
// VU → szikrasűrűség + maximális fényerő.
//
// Viselkedés:
//   - Csend: néhány lassú, halvány szikra (ambient twinkle)
//   - Erős jel: sok szikra, teljes fényerőn
//   - Hue: 8 előre definiált szép szín váltakozva (nem random zaj)
//   - Fade-out: SPARKLE_FADE_STEPS lépésben lineárisan halványodik
// -----------------------------------------------------------------------------

static void renderSparkle() {
  const uint32_t SPARKLE_MS = 18;
  if (millis() - g_sparkleLastFrame < SPARKLE_MS) return;
  g_sparkleLastFrame = millis();

  const uint16_t n    = ledCount();
  const uint16_t half = n / 2;
  uint8_t vuAvg       = (uint8_t)(((uint16_t)g_smoothL + g_smoothR) >> 1);

  // ── Sávhossz simítás (középről kifelé, mint a rainbow) ───────
  // Célhossz: VU-arányos, 0-tól half-ig
  float targetLen = (float)vuMap(vuAvg, half);
  // Gyors felfelé (~3 frame), lassú lefelé (~15 frame)
  float lenCoef = (targetLen > g_sparkleLen) ? 0.35f : 0.07f;
  g_sparkleLen += lenCoef * (targetLen - g_sparkleLen);
  if (g_sparkleLen < 0.0f) g_sparkleLen = 0.0f;
  if (g_sparkleLen > (float)half) g_sparkleLen = (float)half;

  uint16_t activeHalf = (uint16_t)(g_sparkleLen + 0.5f);

  // ── Aktív sávon kívüli szikrák törlése (sáv összehúzódásakor) ─
  for (uint16_t i = 0; i < half; i++) {
    if (i >= activeHalf) {
      g_sparkleBr[half - 1 - i] = 0;   // bal oldal (belső → külső)
      g_sparkleBr[half + i]     = 0;   // jobb oldal
    }
  }

  // ── Új szikrák száma: VU-arányos, csak az aktív sávon belül ──
  // Skálázás: vuAvg 0..255 → 0..activeHalf/2 szikra/frame
  uint16_t newCount = (activeHalf < 2) ? 0
    : (uint16_t)((uint32_t)vuAvg * (activeHalf / 2) / 255U);

  // ── Max fényerő: gyenge jelnél halvány, erős jelnél teljes ───
  uint8_t maxBr = (vuAvg == 0) ? 0
    : (uint8_t)(80 + (uint16_t)vuAvg * 175U / 255U);

  // ── Szikrák elhelyezése szimmetrikusan (középről kifelé) ─────
  static const uint8_t hueTable[8] = { 0, 32, 64, 96, 160, 192, 224, 240 };
  for (uint16_t k = 0; k < newCount; k++) {
    // random pozíció az aktív sávon belül (fél szalag indexe)
    uint16_t pos = (uint16_t)random(activeHalf);
    uint16_t idxL = half - 1 - pos;   // bal szimmetria-pixel
    uint16_t idxR = half + pos;        // jobb szimmetria-pixel
    if (g_sparkleBr[idxL] == 0) {
      uint8_t h = hueTable[random(8)];
      g_sparkleBr[idxL]  = SPARKLE_FADE_STEPS;
      g_sparkleHue[idxL] = h;
      // szimmetrikus jobb oldal: ugyanaz a szikra
      g_sparkleBr[idxR]  = SPARKLE_FADE_STEPS;
      g_sparkleHue[idxR] = h;
    }
  }

  // ── Kirajzolás + fade-out ─────────────────────────────────────
  for (uint16_t i = 0; i < n; i++) {
    if (g_sparkleBr[i] > 0) {
      uint8_t br = (uint8_t)((uint32_t)g_sparkleBr[i] * maxBr / SPARKLE_FADE_STEPS);
      uint16_t hue16 = (uint16_t)g_sparkleHue[i] << 8;
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue16, 220, br)));
      g_sparkleBr[i]--;
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  showStrip();
}
// -----------------------------------------------------------------------------
// Analog (model=3)
// Analóg műszer feeling: fehér sáv + piros csúcsmutató
// L csatorna: bal fél (0 → half-1), R csatorna: jobb fél (half → end)
// Csúcs visszaesés: ~0.5mp (≈ 2.6 pixel/frame @ 18ms)
// -----------------------------------------------------------------------------
static void renderMeterVU() {
  // Simított VU értékek (az envelope follower már kezeli az attack/release-t)
  uint8_t vuL = g_smoothL;
  uint8_t vuR = g_smoothR;

  const uint16_t half  = ledCount() / 2;
  // Csúcs visszaesés: 0.5mp alatt a félszalag hosszát teszi meg.
  // Kevés LED-nél ez pixelben kisebb, de arányában ugyanannyi idő alatt ér le.
  const float    decay = (float)half / (500.0f / LEDSTRIP_FRAME_MS);

  // Aktuális sáv pixel-hossz (vuMap: dinamikus + gamma)
  float litLf = (float)vuMap(vuL, half);
  float litRf = (float)vuMap(vuR, half);
  uint16_t litL = (uint16_t)litLf;
  uint16_t litR = (uint16_t)litRf;

  // Peak: csak felfelé ugrik, lefelé lassan esik
  if (litLf > g_meterPeakL) g_meterPeakL = litLf;
  else                       g_meterPeakL -= decay;
  if (g_meterPeakL < 0.0f)  g_meterPeakL = 0.0f;

  if (litRf > g_meterPeakR) g_meterPeakR = litRf;
  else                       g_meterPeakR -= decay;
  if (g_meterPeakR < 0.0f)  g_meterPeakR = 0.0f;

  uint16_t peakL = (uint16_t)g_meterPeakL;
  uint16_t peakR = (uint16_t)g_meterPeakR;
  if (peakL >= half)  peakL = half - 1;
  if (peakR >= half)  peakR = half - 1;

  clearStrip();

  // Bal fél: 0 = csend (bal széle), half-1 = max → balról jobbra nő
  for (uint16_t i = 0; i < half; i++) {
    if (i < litL)
      strip.setPixelColor(i, rgb(220, 220, 220));
  }
  // Bal csúcs: piros mutató
  if (peakL > 0) {
    uint16_t pidx = peakL < half ? peakL : half - 1;
    strip.setPixelColor(pidx, rgb(255, 0, 0));
    if (pidx + 1 < half)
      strip.setPixelColor(pidx + 1, rgb(180, 0, 0));
  }

  // Jobb fél: half = csend (közép), ledCount()-1 = max → balról jobbra nő
  for (uint16_t i = 0; i < half; i++) {
    if (i < litR)
      strip.setPixelColor(half + i, rgb(220, 220, 220));
  }
  // Jobb csúcs: piros mutató
  if (peakR > 0) {
    uint16_t pidx = half + (peakR < half ? peakR : half - 1);
    strip.setPixelColor(pidx, rgb(255, 0, 0));
    if (pidx + 1 < (uint16_t)ledCount())
      strip.setPixelColor(pidx + 1, rgb(180, 0, 0));
  }

  showStrip();
}

static void renderVolumeBar(uint8_t vol) {
  uint16_t lit = map(vol, 0, 100, 0, ledCount());
  clearStrip();
  for (uint16_t i = 0; i < ledCount(); i++) {
    if (i < lit) {
      strip.setPixelColor(i, vuHsvColor(i, ledCount(), 220));
    }
  }
  showStrip();
}

// --- Stop: Szélforgó ---
// Fázisok: FILL → SPIN (N ciklus) → DRAIN → irányváltás → ismétlés
// Az irány (g_wmDir) meghatározza honnan tölt, merre forog, és honnan ürül.
// g_wmDir = +1: bal→jobb tölt, rainbow jobbra "folyik", bal→jobb ürül (visszacsévél)
// g_wmDir = -1: jobb→bal tölt, rainbow balra "folyik", jobb→bal ürül
// Irányváltás: SPIN_CYCLES ciklus után (pl. 3-szor jobbra, majd 3-szor balra)

enum WindmillPhase : uint8_t { WM_FILL = 0, WM_SPIN, WM_DRAIN };
static WindmillPhase g_wmPhase      = WM_FILL;
static uint16_t      g_wmFill       = 0;    // feltöltött LED-ek száma
static uint32_t      g_wmSpinFrames = 0;    // eltelt frame-ek a spin fázisban
static uint32_t      g_wmLastFrame  = 0;
static uint16_t      g_wmHueBase    = 0;    // rainbow alap-hue
static int8_t        g_wmDir        = 1;    // +1 vagy -1
static uint8_t       g_wmCyclesDone = 0;    // hány irány-ciklus ment le

// Spin ciklusok száma egy irányban
static uint8_t wmSpinCycles() {
  uint16_t n = ledCount();
  if (n <= 20) return 2;
  if (n <= 48) return 3;
  if (n <= 96) return 4;
  return 5;
}

// Hue lépésméret/frame (referencia-sebesség, arányos LED-számmal)
static uint16_t wmHueStep(uint16_t n) {
  return (uint16_t)((uint32_t)320 * 144 / n);
}

// Pixel index az irány és pozíció alapján:
//  dir=+1: i=0 → bal szél (index 0), i=n-1 → jobb szél
//  dir=-1: i=0 → jobb szél (index n-1), i=n-1 → bal szél
static inline uint16_t wmIdx(uint16_t i, uint16_t n, int8_t dir) {
  return (dir > 0) ? i : (n - 1 - i);
}

// Hue egy adott pixelhez: a rainbow iránya követi a dir-t
// dir=+1: hue i=0..n-1 → növekszik  (jobbra forog körben)
// dir=-1: hue i=0..n-1 → csökken    (balra forog körben)
static inline uint16_t wmHue(uint16_t base, uint16_t i, uint16_t n, int8_t dir) {
  uint16_t offset = (uint16_t)((uint32_t)i * 65536UL / n);
  return (dir > 0) ? (base - offset) : (base + offset);
}

static void renderStop() {
  if (g_mode != g_lastMode) {
    g_wmPhase      = WM_FILL;
    g_wmFill       = 0;
    g_wmSpinFrames = 0;
    g_wmHueBase    = (uint16_t)random(65536);
    g_wmDir        = 1;
    g_wmCyclesDone = 0;
    g_lastMode     = g_mode;
  }

  uint32_t frameMs = (g_wmPhase == WM_SPIN) ? 18 : 13;
  if (millis() - g_wmLastFrame < frameMs) return;
  g_wmLastFrame = millis();

  const uint16_t n    = ledCount();
  const uint16_t step = wmHueStep(n);

  if (g_wmPhase == WM_FILL) {
    // ── Feltölt az aktuális irányból, rainbow folyik befelé ──────
    if (g_wmFill < n) g_wmFill++;
    clearStrip();
    for (uint16_t i = 0; i < g_wmFill; i++) {
      uint16_t px  = wmIdx(i, n, g_wmDir);
      uint16_t hue = wmHue(g_wmHueBase, i, n, g_wmDir);
      strip.setPixelColor(px, strip.gamma32(strip.ColorHSV(hue, 255, 210)));
    }
    showStrip();
    if (g_wmFill >= n) {
      g_wmPhase      = WM_SPIN;
      g_wmSpinFrames = 0;
    }

  } else if (g_wmPhase == WM_SPIN) {
    // ── Teli szalag, rainbow forog az irányba N cikluson át ─────
    g_wmHueBase += step * g_wmDir;  // dir=+1: jobbra, dir=-1: balra forog
    for (uint16_t i = 0; i < n; i++) {
      uint16_t px  = wmIdx(i, n, g_wmDir);
      uint16_t hue = wmHue(g_wmHueBase, i, n, g_wmDir);
      strip.setPixelColor(px, strip.gamma32(strip.ColorHSV(hue, 255, 210)));
    }
    showStrip();
    g_wmSpinFrames++;
    // Egy ciklus = 65536 hue-egység / lépésméret
    uint32_t framesPerCycle = 65536UL / step;
    if (framesPerCycle < 1) framesPerCycle = 1;
    if (g_wmSpinFrames >= (uint32_t)wmSpinCycles() * framesPerCycle) {
      g_wmPhase = WM_DRAIN;
      g_wmFill  = n;
    }

  } else { // WM_DRAIN
    // ── Visszacsévél UGYANONNAN ahol töltött (azonos irány) ─────
    // rainbow tovább forog közben
    g_wmHueBase += step * g_wmDir;
    if (g_wmFill > 0) {
      clearStrip();
      // A feltölt 0..g_wmFill-1 LED-et tartjuk, a g_wmFill..n-1 sötét
      for (uint16_t i = 0; i < g_wmFill; i++) {
        uint16_t px  = wmIdx(i, n, g_wmDir);
        uint16_t hue = wmHue(g_wmHueBase, i, n, g_wmDir);
        strip.setPixelColor(px, strip.gamma32(strip.ColorHSV(hue, 255, 210)));
      }
      showStrip();
      g_wmFill--;
    } else {
      // Ciklus vége: irányváltás, újra FILL
      g_wmCyclesDone++;
      g_wmDir    = -g_wmDir;   // irányváltás
      g_wmPhase  = WM_FILL;
      g_wmFill   = 0;
      // Hue eltolás hogy ne ugyanazt a színt lássuk minden ciklus elején
      g_wmHueBase += 21845U;   // ~120° eltolás (65536/3)
    }
  }
}

// Knight Rider (connecting) – pattogó fej, exponenciális farokkal
// Fejméret: 3 LED @ 16, +1 minden duplázásra (log2 skála)
static uint8_t krHeadSize() {
  uint16_t n = ledCount();
  uint8_t  h = 3;
  uint16_t t = 16;
  while (t < n && h < 8) { t <<= 1; h++; }
  return h;
}

static void renderConnecting() {
  const uint32_t KR_MS = 20;
  if (g_mode != g_lastMode) {
    g_krPos      = 0;
    g_krDir      = 1;
    g_lastMode   = g_mode;
  }
  if (millis() - g_krLastFrame < KR_MS) return;
  g_krLastFrame = millis();

  const uint16_t n    = ledCount();
  const uint8_t  head = krHeadSize();   // fej mérete (3..8 LED)
  const uint8_t  tail = head * 3;       // farok hossza (fej háromszorosa)

  clearStrip();

  // Fej: teljes fényerő kék, gamma-korrigált
  for (uint8_t h = 0; h < head; h++) {
    int16_t p = g_krPos - h * g_krDir;  // farok a mozgás ellentétes irányában
    if (p >= 0 && p < (int16_t)n) {
      // h=0: legfényesebb (255), exponenciálisan halványul
      uint8_t br = (uint8_t)(255U >> h);   // 255, 127, 63, 31...
      strip.setPixelColor(p, strip.gamma32(strip.Color(0, 0, br)));
    }
  }
  // Farok: exponenciális halvántulás a fej mögött
  for (uint8_t t = head; t < tail; t++) {
    int16_t p = g_krPos - t * g_krDir;
    if (p >= 0 && p < (int16_t)n) {
      // head..tail: 127 → ~1 exponenciálisan
      uint8_t br = (uint8_t)(127U >> (t - head));
      if (br < 2) br = 0;
      strip.setPixelColor(p, strip.gamma32(strip.Color(0, 0, br)));
    }
  }

  showStrip();

  // Léptetés + pattanás
  g_krPos += g_krDir;
  if (g_krPos >= (int16_t)n) { g_krPos = n - 2; g_krDir = -1; }
  if (g_krPos < 0)            { g_krPos = 1;     g_krDir =  1; }
}

static void renderScreensaver() {
  if (!config.store.lsSsEnabled) {
    strip.clear();
    strip.show();
    return;
  }
  renderSinBreath();
}

static void renderBuffering() {
  renderPulse(255, 110, 0, 8, 120, 2, LEDSTRIP_IDLE_PULSE_MS);
}

static void renderPlay() {
  switch (config.store.lsModel) {
    case 1:
      renderRainbowFlow();
      break;
    case 2:
      renderSparkle();
      break;
    case 3:
      renderMeterVU();
      break;
    default:
    case 0: {
      // Simított VU – a nyers peak értékek idegesen ugrálnának
      if (g_smoothL > 0 || g_smoothR > 0) {
        renderStereoVU(g_smoothL, g_smoothR);
      } else {
        renderRainbow();
      }
      break;
    }
  }
}

static void updateModeFromRuntime() {
  if (isScreensaverMode()) {
    g_mode = LM_SCREENSAVER;
    return;
  }

  if (millis() < g_volumeUntil) {
    g_mode = LM_VOLUME;
    return;
  }

  if (network.status != CONNECTED && network.status != SDREADY) {
    g_mode = LM_CONNECTING;
    return;
  }

  int s = player.status();

  if (s == PR_PLAY) {
    g_mode = LM_PLAY;
  } else if (s == PR_STOP) {
    g_mode = LM_STOP;
  } else {
    g_mode = LM_BUFFERING;
  }
}

// -----------------------------------------------------------------------------
// plugin
// -----------------------------------------------------------------------------

LedStripPlugin ledStripPlugin;   // globális példány

LedStripPlugin::LedStripPlugin() {}

void ledstripPluginInit() {
    pm.add(&ledStripPlugin);
}

void LedStripPlugin::on_setup() {
  if (!ledStripPinAvailable()) {
    log_i("##[LEDSTRIP]# disabled, no valid output GPIO configured");
    return;
  }
  vuSmoothInit();   // exponenciális VU envelope follower koefficiensek init
  strip.begin();
  strip.setBrightness(map(config.store.lsBrightness, 0, 100, 0, 255));
  strip.clear();
  strip.show();

  g_mode = LM_BOOT;
  g_lastVolume = config.store.volume;
  // Nincs fillAll – az első on_loop() hívás rögtön KR-t rajzol
}

void LedStripPlugin::on_connect() {
  if (!ledStripPinAvailable()) return;
  g_connectedSeen = true;
  flashNow(0, 80, 255, 220);
}

void LedStripPlugin::on_start_play() {
  if (!ledStripPinAvailable()) return;
  g_mode = LM_PLAY;
  flashNow(0, 180, 40, 140);
}

void LedStripPlugin::on_stop_play() {
  if (!ledStripPinAvailable()) return;
  g_mode = LM_STOP;
  fillAll(180, 0, 0);
  showStrip();
}

void LedStripPlugin::on_station_change() {
  if (!ledStripPinAvailable()) return;
  flashNow(0, 220, 180, 180);
}

void LedStripPlugin::on_track_change() {
  if (!ledStripPinAvailable()) return;
  flashNow(255, 255, 255, 120);
}

void LedStripPlugin::on_ticker() {
  if (!ledStripPinAvailable()) return;
  uint8_t vol = config.store.volume;
  if (vol != g_lastVolume) {
    g_lastVolume = vol;
    g_volumeUntil = millis() + LEDSTRIP_VOL_TIMEOUT_MS;
  }
}

void LedStripPlugin::on_loop() {
  if (!ledStripPinAvailable()) return;

  // Ha a plugin le van tiltva WebUI-ból → szalag sötét
  if (!config.store.lsEnabled) {
    strip.clear();
    strip.show();
    return;
  }

  // Brightness frissítése, ha változott a WebUI-ban
  static uint8_t s_lastBr = 255;
  if (config.store.lsBrightness != s_lastBr) {
    s_lastBr = config.store.lsBrightness;
    strip.setBrightness(map(s_lastBr, 0, 100, 0, 255));
  }

  if (millis() - g_lastFrame < LEDSTRIP_FRAME_MS) return;
  g_lastFrame = millis();

  // VU simítás egyszer fut frame-enként, minden render előtt
  {
    uint8_t rawL = fusion_led_vu_left();
    uint8_t rawR = fusion_led_vu_right();
    updateSmoothedVU(rawL, rawR);
  }

  if (millis() < g_flashUntil) {
    renderFlash();
    return;
  }

  updateModeFromRuntime();

  switch (g_mode) {
    case LM_BOOT:
      renderConnecting();   // KR effekt boot közben is
      break;

    case LM_CONNECTING:
      renderConnecting();
      break;

    case LM_STOP:
      renderStop();
      break;

    case LM_PLAY:
      renderPlay();
      break;

    case LM_BUFFERING:
      renderBuffering();
      break;

    case LM_VOLUME:
      renderVolumeBar(g_lastVolume);
      break;

    case LM_SCREENSAVER:
      renderScreensaver();
      break;

    default:
      renderStop();
      break;
  }
}
