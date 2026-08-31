#include "namedays.h"

#ifdef NAMEDAYS_FILE
#if NAMEDAYS_FILE == HU
  #include "../../locale/namedays/namedays_HU.h"
#elif NAMEDAYS_FILE == PL
  #include "../../locale/namedays/namedays_PL.h"
#elif NAMEDAYS_FILE == CZ
  #include "../../locale/namedays/namedays_CZ.h"
#elif NAMEDAYS_FILE == GR
  #include "../../locale/namedays/namedays_GR.h"
#elif NAMEDAYS_FILE == NL
  #include "../../locale/namedays/namedays_NL.h"
#elif NAMEDAYS_FILE == UA
  #include "../../locale/namedays/namedays_UA.h"
#elif NAMEDAYS_FILE == DE
  #include "../../locale/namedays/namedays_DE.h"
#else
  #error "Unsupported NAMEDAYS_FILE"
#endif

#include <string.h>

static inline int idx_from_md(uint8_t m, uint8_t d) {
    static const uint8_t mdays[12] = {31,29,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return -1;
    if (d < 1 || d > mdays[m-1]) return -1;
    int idx = d - 1;
    for (uint8_t i = 1; i < m; ++i) idx += mdays[i-1];
    return idx; // 0..365
}

static inline void normalize_commas(char* s, size_t cap) {
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] == ',' && s[i+1] && s[i+1] != ' ') {
            size_t len = strlen(s);
            if (len + 1 < cap) { memmove(&s[i+2], &s[i+1], len - i); s[i+1] = ' '; }
        }
    }
}

bool namedays_get_str(uint8_t month, uint8_t day, char* out, size_t outlen) {
    if (!out || !outlen) return false;
    out[0] = '\0';
    int idx = idx_from_md(month, day);
    if (idx < 0) return false;

    const char* raw = namedays[idx];
    if (!raw || !*raw) return false;

    strlcpy(out, raw, outlen);
    normalize_commas(out, outlen); // "Alpár,Fruzsina" → "Alpár, Fruzsina"
    return out[0] != '\0';
}

#else
bool namedays_get_str(uint8_t, uint8_t, char*, size_t) { return false; }
#endif
