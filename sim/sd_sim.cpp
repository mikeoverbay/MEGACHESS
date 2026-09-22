// Megachess simulator - the SD library over a folder.
#include <SD.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <string.h>

static std::string root = "SD_DATA";

void sd_sim_set_root(const char* r) { root = r; }

// The real library only understands 8.3 names - up to eight characters, one
// dot, up to three more, none of |<>^+=?[];,*"\ - and fails silently on
// anything else, directories included. A nine-letter folder cost a day. Refuse
// the same names here so the simulator shows it.
static bool legal83(const char* path) {
    int n = 0, e = -1;                       // chars in the name part; -1 = no dot yet
    for (const char* c = path; ; c++) {
        if (*c == '/' || *c == '\\' || *c == 0) {
            if (n > 8 || e > 3) return false;
            n = 0; e = -1;
            if (*c == 0) return true;
            continue;
        }
        if (*c == '.') { if (e >= 0) return false; e = 0; continue; }
        if (*c < 0x21 || *c > 0x7E || strchr("|<>^+=?[];,*\"\\", *c)) return false;
        if (e >= 0) e++; else n++;
    }
}

static std::string host(const char* path) {
    std::string p = root;
    if (!path || !*path) return p;
    if (!legal83(path)) {
        fprintf(stderr, "sd_sim: not an 8.3 name, the card library would fail: %s\n", path);
        return p + "/__not_8_3__/" + path;   // a place that never exists
    }
    if (path[0] != '/' && path[0] != '\\') p += '/';
    for (const char* c = path; *c; c++) p += (*c == '\\') ? '/' : *c;
    return p;
}

// --- File --------------------------------------------------------------------
int File::read() {
    if (!f) return -1;
    const int c = fgetc(f);
    return c == EOF ? -1 : c;
}

int File::read(void* buf, size_t n) {
    if (!f) return -1;
    return (int) fread(buf, 1, n, f);
}

bool File::seek(uint32_t pos) { return f && fseek(f, (long) pos, SEEK_SET) == 0; }

uint32_t File::position() { return f ? (uint32_t) ftell(f) : 0; }

uint32_t File::size() {
    if (!f) return 0;
    const long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    const long s = ftell(f);
    fseek(f, cur, SEEK_SET);
    return (uint32_t) s;
}

int File::available() { return f ? (int) (size() - position()) : 0; }

void File::close() {
    if (f) { fclose(f); f = nullptr; }
    dir = false;
}

const char* File::name() {
    const size_t s = path.find_last_of("/\\");
    nameBuf = (s == std::string::npos) ? path : path.substr(s + 1);
    return nameBuf.c_str();
}

File File::openNextFile() {
    while (dir && next < entries.size()) {
        const std::string& e = entries[next++];
        const std::string full = path + "/" + e;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (st.st_mode & S_IFDIR) return File(full, {});
        FILE* fh = fopen(full.c_str(), "rb");
        if (fh) return File(fh, full);
    }
    return File();
}

// --- SD ----------------------------------------------------------------------
SDClass SD;

File SDClass::open(const char* path, uint8_t mode) {
    const std::string p = host(path);
    struct stat st;
    if (stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
        std::vector<std::string> ents;
        struct _finddata_t fd;
        intptr_t h = _findfirst((p + "/*").c_str(), &fd);
        if (h != -1) {
            do { if (fd.name[0] != '.') ents.push_back(fd.name); } while (_findnext(h, &fd) == 0);
            _findclose(h);
        }
        return File(p, ents);
    }
    FILE* fh = fopen(p.c_str(), mode == FILE_WRITE ? "ab+" : "rb");
    return fh ? File(fh, p) : File();
}

bool SDClass::exists(const char* path) {
    struct stat st;
    return stat(host(path).c_str(), &st) == 0;
}

bool SDClass::remove(const char* path) { return ::remove(host(path).c_str()) == 0; }

bool SDClass::mkdir(const char* path) { return _mkdir(host(path).c_str()) == 0; }
