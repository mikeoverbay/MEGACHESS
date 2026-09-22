// Megachess simulator - runs the real sketch against the framebuffer panel and
// the folder-backed SD, feeds it a tap script, and dumps frames.
//
//   megasim.exe <sdcard-root> <out-dir> [script...]
//
//   x,y          queue a tap at screen pixel x,y
//   snap:NAME    run the sketch until every queued tap is consumed, then save
//   wait         run the sketch until idle (lets the engine move)
//   showconfirm  draw the NEW-game dialog directly and snap it (visual check)
//   showleave    the same for the MENU dialog
//   showthinking draw the status card as it looks mid-search and snap it
//
// Taps are queued, not applied, so a script can answer a dialog the sketch
// opens on the previous tap. build.py wraps all of this.
#include <SD.h>
#include "../megachess.h"
#include "megachess_protos.h"
#include "../Megachess.ino"

#include <string>

static std::string outDir;
static int frameNo = 0;

static void snap(const std::string& tag) {
    char p[512];
    snprintf(p, sizeof p, "%s/%02d_%s.ppm", outDir.c_str(), frameNo++, tag.c_str());
    tft.dump(p);
    fprintf(stderr, "frame %s\n", p);
}

// Run the sketch until the tap queue is empty and it has had a few idle
// passes - enough for the engine to reply.
static void drain() {
    int guard = 0;
    while (SimPanel::busy() && guard++ < 20000) loop();
    for (int i = 0; i < 3; i++) { loop(); sim_advance(50); }
    sim_advance(300);                        // past the debounce
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: megasim <sdroot> <outdir> [script...]\n"); return 2; }
    sd_sim_set_root(argv[1]);
    outDir = argv[2];

    setup();
    snap("boot");

    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "wait") { drain(); for (int k = 0; k < 10; k++) { loop(); sim_advance(50); } continue; }
        if (a.rfind("snap:", 0) == 0) { drain(); snap(a.substr(5)); continue; }
        if (a == "showconfirm") { drain(); ui_confirm_draw(F("NEW GAME?"), F("current game is lost")); snap("confirm"); continue; }
        if (a == "showleave") { drain(); ui_confirm_draw(F("LEAVE GAME?"), F("RESUME gets it back")); snap("leave"); continue; }
        if (a == "showthinking") {           // the card as it looks mid-search
            drain();
            ai_thinking = true; ui_set_status(F("THINKING"), cur.accent); ui_draw_panel();
            snap("thinking");
            ai_thinking = false; ui_set_status(NULL, 0); ui_draw_panel();
            continue;
        }
        int x, y;
        if (sscanf(a.c_str(), "%d,%d", &x, &y) == 2) { SimPanel::queueTap(x, y); continue; }
        fprintf(stderr, "unknown script item: %s\n", a.c_str());
    }
    drain();
    return 0;
}
