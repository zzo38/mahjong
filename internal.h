
#include "mahjong.h"

enum {
  VIS_I_HAND,
  VIS_I_LIVE_WALL,
  VIS_I_DEAD_WALL,
  VIS_I_KAN,
};

#define VIS_V_FRONT 0x01
#define VIS_V_BACK 0x02
#define VIS_V_MIXED 0x04
#define VIS_V_STACKED 0x08

typedef struct {
  uint8_t team;
} PlayerRules;

typedef struct Mahjong_Rules {
  int8_t loaded;
  uint32_t(*random_call)(void*,uint32_t);
  void*random_misc;
  uint16_t ndeck;
  uint16_t*deck;
  uint8_t nplayer;
  PlayerRules player[4];
} Rules;

typedef struct Mahjong_Game {
  Rules*rules;
} Game;

