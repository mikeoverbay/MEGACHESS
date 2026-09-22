// Megachess simulator - the SD library over a folder on disk.
//
// sd_sim_set_root() points it at the sdcard/ staging folder, so the simulator
// reads exactly the .SET, BOOK.TXT and SPLASH.IMG the card carries and writes
// its saves, settings and PGN there too.
#pragma once
#include <Arduino.h>
#include <cstdio>
#include <string>
#include <vector>

#define FILE_READ  0
#define FILE_WRITE 1

void sd_sim_set_root(const char* root);

class File : public Print {
public:
    File() {}
    explicit File(FILE* fh, const std::string& p) : f(fh), path(p) {}
    File(const std::string& p, const std::vector<std::string>& ents) : path(p), dir(true), entries(ents) {}

    operator bool() const { return f != nullptr || dir; }
    size_t   write(uint8_t c) override { return f ? (fputc(c, f) != EOF ? 1 : 0) : 0; }
    using Print::write;
    int      read();
    int      read(void* buf, size_t n);
    bool     seek(uint32_t pos);
    uint32_t position();
    uint32_t size();
    int      available();
    void     close();
    const char* name();
    bool     isDirectory() const { return dir; }
    File     openNextFile();

private:
    FILE*       f = nullptr;
    std::string path;
    bool        dir = false;
    std::vector<std::string> entries;
    size_t      next = 0;
    std::string nameBuf;
};

class SDClass {
public:
    bool begin(uint8_t) { return true; }
    File open(const char* path, uint8_t mode = FILE_READ);
    bool exists(const char* path);
    bool remove(const char* path);
    bool mkdir(const char* path);
};
extern SDClass SD;
