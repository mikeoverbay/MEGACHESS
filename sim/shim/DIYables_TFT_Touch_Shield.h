// Megachess simulator - both DIYables driver classes become the framebuffer
// panel, so megachess.h's typedef works unchanged whichever driver is chosen.
#pragma once
#include "../panel_sim.h"

class DIYables_TFT_HX8357D_Shield : public SimPanel {};
class DIYables_TFT_RM68140_Shield : public SimPanel {};
typedef SimPanel DIYables_TFT_Touch_Shield_Base;
