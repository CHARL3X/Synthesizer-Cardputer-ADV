#include "sd_store.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>

#include "../config.h"

namespace sdstore {

namespace {
bool gAvail = false;
const char* gErr = "not started";
void setErr(const char* e) { gErr = e; }

// Build "<kSdDir>/<sanitised name><kSdExt>" into out. The name is reduced to
// filename-safe chars (a..z 0..9 - _), lowercased, truncated to kMaxNameLen,
// and never empty. Returns false if it couldn't fit.
bool makePath(const char* name, char* out, int cap) {
    char safe[kMaxNameLen + 1];
    int n = 0;
    for (const char* c = name; *c && n < kMaxNameLen; ++c) {
        char ch = *c;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                        ch == '-' || ch == '_';
        if (ok) safe[n++] = ch;
    }
    if (n == 0) { safe[n++] = 'p'; safe[n++] = 'a'; safe[n++] = 't'; safe[n++] = 'c'; safe[n++] = 'h'; }
    safe[n] = '\0';
    const int need = (int)(strlen(cfg::kSdDir) + 1 + strlen(safe) + strlen(cfg::kSdExt) + 1);
    if (need > cap) return false;
    strcpy(out, cfg::kSdDir);
    strcat(out, "/");
    strcat(out, safe);
    strcat(out, cfg::kSdExt);
    return true;
}

// Basename: the part after the last '/'. SD's File::name() returns either a
// full path or a bare name depending on the core version — handle both.
const char* baseName(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool endsWithExt(const char* name) {
    const size_t ln = strlen(name), le = strlen(cfg::kSdExt);
    return ln >= le && strcmp(name + ln - le, cfg::kSdExt) == 0;
}
}  // namespace

bool begin() {
    if (!cfg::kSdEnabled) { setErr("SD disabled"); return false; }
    if (gAvail) return true;
    // Drive the SD's SPI lines explicitly (the bus is shared with the display;
    // these pins are the hardware-unverified part — see config.h).
    SPI.begin(cfg::kSdSckPin, cfg::kSdMisoPin, cfg::kSdMosiPin, cfg::kSdCsPin);
    if (!SD.begin(cfg::kSdCsPin, SPI, cfg::kSdFreqHz)) {
        setErr("no card / SD init failed");
        gAvail = false;
        return false;
    }
    if (!SD.exists(cfg::kSdDir) && !SD.mkdir(cfg::kSdDir)) {
        setErr("cannot create /glide");
        gAvail = false;
        return false;
    }
    gAvail = true;
    setErr("ok");
    return true;
}

bool available() { return gAvail; }
const char* lastError() { return gErr; }

bool save(const char* name, const store::PatchData& pd) {
    if (!gAvail && !begin()) return false;
    uint8_t buf[512];
    const size_t n = store::encodePatch(pd, buf, sizeof buf);
    if (n == 0) { setErr("encode failed"); return false; }
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    File f = SD.open(path, FILE_WRITE);  // "w" — truncates/creates (overwrite)
    if (!f) { setErr("open for write failed"); return false; }
    const size_t wrote = f.write(buf, n);
    f.close();
    if (wrote != n) { setErr("short write (card full?)"); return false; }
    setErr("saved");
    return true;
}

bool load(const char* name, store::PatchData& out) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close(); setErr("not found"); return false; }
    uint8_t buf[512];
    const size_t len = (size_t)f.size();
    if (len == 0 || len > sizeof buf) { f.close(); setErr("bad file size"); return false; }
    const size_t got = f.read(buf, len);
    f.close();
    if (got != len) { setErr("read error"); return false; }
    if (!store::decodePatch(buf, len, out)) { setErr("corrupt patch"); return false; }
    setErr("loaded");
    return true;
}

bool remove(const char* name) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    if (!SD.exists(path)) { setErr("not found"); return false; }
    if (!SD.remove(path)) { setErr("delete failed"); return false; }
    setErr("deleted");
    return true;
}

int list(char names[][kMaxNameLen + 1], int max) {
    if (!gAvail && !begin()) return -1;
    File dir = SD.open(cfg::kSdDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); setErr("no library dir"); return -1; }
    int count = 0;
    const int cap = (max < kMaxList) ? max : kMaxList;
    for (File e = dir.openNextFile(); e && count < cap; e = dir.openNextFile()) {
        if (!e.isDirectory()) {
            const char* bn = baseName(e.name());
            if (endsWithExt(bn)) {
                int k = 0;
                const int stop = (int)strlen(bn) - (int)strlen(cfg::kSdExt);  // drop ".gpat"
                for (; k < stop && k < kMaxNameLen; ++k) names[count][k] = bn[k];
                names[count][k] = '\0';
                ++count;
            }
        }
        e.close();
    }
    dir.close();
    setErr("ok");
    return count;
}

}  // namespace sdstore
