#pragma once
#ifdef USE_DLNA
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_dlnaHttpMux;

struct DlnaHttpGuard {
  SemaphoreHandle_t mutex;

  DlnaHttpGuard() : mutex(g_dlnaHttpMux) {
    if (mutex) xSemaphoreTake(mutex, portMAX_DELAY);
  }

  ~DlnaHttpGuard() {
    if (mutex) xSemaphoreGive(mutex);
  }

  DlnaHttpGuard(const DlnaHttpGuard&) = delete;
  DlnaHttpGuard& operator=(const DlnaHttpGuard&) = delete;
};
#endif
