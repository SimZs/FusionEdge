#include "../core/options.h"
#ifdef USE_DLNA

#include "../core/config.h"
#include "dlna_worker.h"
#include "dlna_service.h"
#include <LittleFS.h>
#include "esp_task_wdt.h"
#include <esp_heap_caps.h>
#include "dlna_index.h"
#include "dlna_http_guard.h" 

QueueHandle_t g_dlnaQueue = nullptr;
SemaphoreHandle_t g_littlefsMux = nullptr;
DlnaStatus g_dlnaStatus = {false,false,0,0,0,{0}};

static TaskHandle_t s_workerTask = nullptr;
static uint32_t s_playlistVer = 1;

volatile bool g_dlnaPlaylistDirty = false;
volatile bool g_dlnaPlaylistActivatePending = false;
volatile bool g_webPlaylistActivatePending = false;

uint32_t dlna_playlist_version() { return s_playlistVer; }

static void bumpPlaylistVer() {
  s_playlistVer++;
  if (s_playlistVer == 0) s_playlistVer = 1;
  g_dlnaStatus.playlistVer = s_playlistVer;
}

void dlna_status_setBusy(const DlnaJob& j, const char* msg) {
  g_dlnaStatus.busy = true;
  g_dlnaStatus.ok = false;
  g_dlnaStatus.err = 0;
  g_dlnaStatus.reqId = j.reqId;
  strncpy(g_dlnaStatus.msg, msg ? msg : "busy", sizeof(g_dlnaStatus.msg)-1);
  g_dlnaStatus.msg[sizeof(g_dlnaStatus.msg)-1] = 0;
}

void dlna_status_setDone(const DlnaJob& j, bool ok, int err, const char* msg) {
  g_dlnaStatus.busy = false;
  g_dlnaStatus.ok = ok;
  g_dlnaStatus.err = err;
  g_dlnaStatus.reqId = j.reqId;
  strncpy(g_dlnaStatus.msg, msg ? msg : (ok ? "ok" : "fail"), sizeof(g_dlnaStatus.msg)-1);
  g_dlnaStatus.msg[sizeof(g_dlnaStatus.msg)-1] = 0;
}

/*
static bool write_playlist_atomic(const char* tmpPath, const char* finalPath, const String& content) {
  // mutex: egy időben ne olvassa/írja más
  xSemaphoreTake(g_littlefsMux, portMAX_DELAY);

  File f = LittleFS.open(tmpPath, FILE_WRITE);
  if (!f) { xSemaphoreGive(g_littlefsMux); return false; }
  size_t n = f.print(content);
  f.close();
  if (n != content.length()) { LittleFS.remove(tmpPath); xSemaphoreGive(g_littlefsMux); return false; }

  LittleFS.remove(finalPath);
  bool ok = LittleFS.rename(tmpPath, finalPath);

  xSemaphoreGive(g_littlefsMux);
  return ok;
}

static bool append_playlist_atomic(const char* tmpPath, const char* finalPath, const String& addLines) {
  xSemaphoreTake(g_littlefsMux, portMAX_DELAY);

  // 1) read old
  String old;
  {
    File f = LittleFS.open(finalPath, FILE_READ);
    if (f) { old = f.readString(); f.close(); }
  }

  // 2) write merged to tmp
  File t = LittleFS.open(tmpPath, FILE_WRITE);
  if (!t) { xSemaphoreGive(g_littlefsMux); return false; }
  t.print(old);
  if (old.length() && old[old.length()-1] != '\n') t.print("\n");
  t.print(addLines);
  t.close();

  // 3) swap
  LittleFS.remove(finalPath);
  bool ok = LittleFS.rename(tmpPath, finalPath);

  xSemaphoreGive(g_littlefsMux);
  return ok;
}

*/
// WDT/yield helper hosszú ciklusokba
static inline void worker_yield() {
  //esp_task_wdt_reset();
  vTaskDelay(1); // 1 tick yield
}

static void dlna_worker_task(void* ) {
  //esp_task_wdt_add(nullptr); // add current task to WDT (ha használod)
  DlnaJob j{};

  for (;;) {
    if (xQueueReceive(g_dlnaQueue, &j, portMAX_DELAY) != pdTRUE) continue;

    Serial.printf("[DLNA][WORK] job=%d objectId='%s'\n", (int)j.type, j.objectId);

    if (j.type == DJ_CANCEL) {
      dlna_status_setDone(j, true, 0, "cancelled");
      continue;
    }

    dlna_status_setBusy(j, "working");

    bool ok = false;
   // int err = 0;

    // !!! FONTOS: itt semmilyen AsyncWebServerRequest nincs, csak paraméterek
    if (j.type == DJ_LIST) {
      dlna_status_setBusy(j, "list");

      if (!g_dlnaControlUrl.length()) {
        dlna_status_setDone(j, false, 503, "DLNA not initialized");
        continue;
      }

      DlnaIndex idx;
      String json;
      log_i("##[DLNA]# worker list begin objectId='%s' start=%u",
            j.objectId, static_cast<unsigned>(j.start));
      ok = idx.listContainer(g_dlnaControlUrl, j.objectId, json, j.start);
      if (!ok) {
        dlna_status_setDone(j, false, 500, "list failed");
        continue;
      }

      xSemaphoreTake(g_littlefsMux, portMAX_DELAY);
      File out = LittleFS.open(DLNA_BROWSE_JSON_PATH, "w");
      const size_t written = out ? out.print(json) : 0;
      if (out) out.close();
      xSemaphoreGive(g_littlefsMux);

      if (written != json.length()) {
        xSemaphoreTake(g_littlefsMux, portMAX_DELAY);
        LittleFS.remove(DLNA_BROWSE_JSON_PATH);
        xSemaphoreGive(g_littlefsMux);
        dlna_status_setDone(j, false, 500, "list write failed");
        continue;
      }

      log_i("##[DLNA]# worker list ready objectId='%s' start=%u bytes=%u",
            j.objectId, static_cast<unsigned>(j.start),
            static_cast<unsigned>(written));
      dlna_status_setDone(j, true, 0, "list ok");
    }
    else if (j.type == DJ_BUILD) {
      dlna_status_setBusy(j, "build");

      if (!g_dlnaControlUrl.length()) {
        dlna_status_setDone(j, false, 503, "DLNA not initialized");
        continue;
      }

      DlnaIndex idx;
      bool hasItems = false, hasContainers = false;

      ok = idx.browseAndDecide(g_dlnaControlUrl, j.objectId, hasItems, hasContainers);

      
      if (!ok) {
        dlna_status_setDone(j, false, 500, "browse failed");
        continue;
      }

      if (!hasItems) { 
       // van konténer, de track nincs közvetlenül -> UI-nak jelezzük szépen
       dlna_status_setDone(j, false, 422, hasContainers ? "No tracks in this container (only subfolders)" : "Empty container");
       continue;
      } 

      uint8_t depth;
      if (hasItems) depth = 2;
      else depth = 6;

      uint32_t limit = j.hardLimit ? j.hardLimit : 20000;

      ok = idx.autoBuildPlaylist(
             g_dlnaControlUrl,
             j.objectId,
             depth,
             limit    // ha nincs: add meg defaultnak
           );

      if (!ok) {
        dlna_status_setDone(j, false, 500, "build failed");
        continue;
      }

      // ===== ATOMIKUS CSERE =====
      xSemaphoreTake(g_littlefsMux, portMAX_DELAY);
      LittleFS.remove(PLAYLIST_DLNA_PATH);
      ok = LittleFS.rename(TMP_PATH, PLAYLIST_DLNA_PATH);
      if (ok) { LittleFS.remove(INDEX_DLNA_PATH); }
      xSemaphoreGive(g_littlefsMux);

      if (!ok) {
        dlna_status_setDone(j, false, 550, "rename failed");
        continue;
      }

      config.sdResumePos = 0;
      config.resumeAfterModeChange = false;

      // 🔑 DLNA build -> reset DLNA index ONLY
      config.store.lastDlnaStation = 1;
      config.saveValue(&config.store.lastDlnaStation, (uint16_t)1);

      g_dlnaPlaylistDirty = true;

      dlna_status_setDone(j, true, 0, "build ok");
    }

    else if (j.type == DJ_APPEND) {
      dlna_status_setBusy(j, "append");

      if (!g_dlnaControlUrl.length()) {
        dlna_status_setDone(j, false, 503, "DLNA not initialized");
        continue;
      }

      DlnaIndex idx;
      bool hasItems = false, hasContainers = false;

      ok = idx.browseAndDecide(g_dlnaControlUrl, j.objectId, hasItems, hasContainers);
      if (!ok) {
        dlna_status_setDone(j, false, 500, "browse failed");
        continue;
      }

      if (!hasItems) { 
       // van konténer, de track nincs közvetlenül -> UI-nak jelezzük szépen
       dlna_status_setDone(j, false, 422, hasContainers ? "No tracks in this container (only subfolders)" : "Empty container");
       continue;
      } 

      uint8_t depth;
      if (hasItems) depth = 2;
      else depth = 6;

      uint32_t limit = j.hardLimit ? j.hardLimit : 20000;

      ok = idx.autoBuildPlaylist(
             g_dlnaControlUrl,
             j.objectId,
             depth,
             limit
           );

      if (!ok) {
        dlna_status_setDone(j, false, 500, "append build failed");
        continue;
      }

      // ===== APPEND =====
      xSemaphoreTake(g_littlefsMux, portMAX_DELAY);

      File out = LittleFS.open(PLAYLIST_DLNA_PATH, FILE_APPEND);
      File in  = LittleFS.open(TMP_PATH, FILE_READ);

      if (out && in) {
        if (out.size() > 0) out.print("\n");
        while (in.available()) out.write(in.read());
        ok = true;
      } else {
        ok = false;
      }

      if (out) out.close();
      if (in)  in.close();
      LittleFS.remove(TMP_PATH);

      xSemaphoreGive(g_littlefsMux);

      if (!ok) {
        dlna_status_setDone(j, false, 551, "append failed");
        continue;
      }

      bumpPlaylistVer();
      g_dlnaPlaylistDirty = true;
      dlna_status_setDone(j, true, 0, "append ok");
    }

else if (j.type == DJ_INIT) {
  dlna_status_setBusy(j, "init");

  String errStr;
  String rootId = String(dlnaIDX);
  log_i("##[DLNA]# init rootObjectId='%s'", rootId.c_str());

  vTaskDelay(1);

  bool okInit = dlnaInit(rootId, errStr);

  vTaskDelay(1);

  dlna_status_setDone(
    j,
    okInit,
    okInit ? 0 : 503,
    okInit ? "init ok" : errStr.c_str()
  );
}

    worker_yield();

    static UBaseType_t lowestStackRemaining = static_cast<UBaseType_t>(-1);
    const UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(nullptr);
    if (stackRemaining < lowestStackRemaining) {
      lowestStackRemaining = stackRemaining;
      log_i("##[DLNA]# worker stack minimum remaining=%u bytes",
            static_cast<unsigned>(stackRemaining));
    }
  }
}

void dlna_worker_start() {
  if (s_workerTask) return;                 // már fut
  if (!g_dlnaHttpMux) {
	  g_dlnaHttpMux = xSemaphoreCreateMutex();
	  Serial.println("[DLNA] HTTP mutex created");}
  if (!g_littlefsMux) g_littlefsMux = xSemaphoreCreateMutex();
  if (!g_dlnaQueue) g_dlnaQueue = xQueueCreate(6, sizeof(DlnaJob));

  const size_t internalLargestBefore =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  // This task opens LittleFS files. Flash reads disable the cache briefly, so
  // its active stack must remain in internal RAM. The previous 24 KB reserve
  // was unnecessarily large; 12 KB keeps TLS/WebUI headroom while the runtime
  // high-water mark below verifies the real margin.
  BaseType_t ok = xTaskCreatePinnedToCore(
    dlna_worker_task, "dlna_worker", 12 * 1024, nullptr, 0, &s_workerTask, 0);

  if (ok != pdPASS) {
    s_workerTask = nullptr;
    Serial.println("[DLNA] worker task create FAILED");
  } else {
    log_i("##[DLNA]# worker ready (stack=internal/12KB, internal largest=%u->%u)",
          static_cast<unsigned>(internalLargestBefore),
          static_cast<unsigned>(heap_caps_get_largest_free_block(
              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    Serial.println("[DLNA] worker task started");
  }
}


bool dlna_worker_enqueue(const DlnaJob& j) {
  if (!g_dlnaQueue) return false;

  // Reserve the single DLNA worker immediately. Without this, another HTTP
  // callback can enqueue a duplicate job before the worker starts running.
  dlna_status_setBusy(j, "queued");
  if (xQueueSend(g_dlnaQueue, &j, 0) == pdTRUE) return true;

  dlna_status_setDone(j, false, 503, "queue full");
  return false;
}
#endif   // USE_DLNA
