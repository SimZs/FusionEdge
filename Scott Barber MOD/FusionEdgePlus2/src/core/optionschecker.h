#pragma once

#if REAL_LEDBUILTIN==TFT_RST
#  error LED_BUILTIN IS THE SAME AS TFT_RST. Check it in myoptions.h
#endif

#if !(defined(ARDUINO_ESP32_DEV) || defined(ARDUINO_ESP32S3_DEV) || defined(ARDUINO_ESP32C3_DEV))
#  error ONLY MODULES "ESP32 Dev Module", "ESP32 Wrover Module" AND "ESP32 S3 Dev Module" ARE SUPPORTED. PLEASE SELECT ONE OF THEM IN THE MENU >> TOOLS >> BOARD
#endif

#if defined(IMPERIALUNIT) && LANGUAGE != EN
#  error IMPERIALUNIT currently requires LANGUAGE EN because the imperial weather labels are defined in language_en.h
#endif

#if defined(USE_LASTFM_COVER) && !defined(LASTFM_API_KEY)
#  error USE_LASTFM_COVER requires LASTFM_API_KEY in myoptions.h
#endif
