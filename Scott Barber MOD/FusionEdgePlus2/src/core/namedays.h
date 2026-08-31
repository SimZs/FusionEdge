#pragma once

#include "options.h"

#ifdef NAMEDAYS_FILE

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Egész napi névnaplista visszaadása vesszővel elválasztva.
// Pl.: "Alpár, Fruzsina"  (month=1..12, day=1..31)
// Visszatér true-val ha van névnap, false-szal ha nincs vagy érvénytelen.
bool namedays_get_str(uint8_t month, uint8_t day, char* out, size_t outlen);

#endif // NAMEDAYS_FILE
