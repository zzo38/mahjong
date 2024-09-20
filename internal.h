
#include "mahjong.h"

enum {
  VIS_I_HAND,
  VIS_I_LIVE_WALL,
  VIS_I_DEAD_WALL,
  VIS_I_KAN,
  VIS_I_FLOWERS,
  VIS_I_DORA,
  VIS_I_URADORA,
  VIS_NI, // last one
};

#define VIS_V_NONE 0x00
#define VIS_V_FRONT 0x01
#define VIS_V_BACK 0x02
#define VIS_V_MIXED 0x04
#define VIS_V_STACKED 0x08
#define VIS_V_OWNER 0x80

#define VF_NULL 0x00
#define VF_INIT 0x01
#define VF_GAME 0x02
#define VF_ROUND 0x03
#define VF_DEAL 0x04
#define VF_NONREPEAT 0x05
#define VF_TEAM 0x80

#define SHOW_TSUMOGIRI 0x01
#define SHOW_DEALT 0x02
#define SHOW_TENPAI 0x04
#define SHOW_OVERCLAIM 0x08

#define ROUND_E 0x00
#define ROUND_S 0x01
#define ROUND_W 0x02
#define ROUND_N 0x03
#define ROUND_PROGRAM 0x40
#define ROUND_GOTO 0x80
#define ROUND_STOP 0xFF

#define TY_NULL 0x00
#define TY_NUMBER 0x01
#define TY_TILE 0x02
#define TY_YAKU 0x03
#define TY_PLAYER 0x04
#define TY_DIR 0x05
#define TY_IRREG 0x06
#define TY_FUNCTION 0x07
#define TY_BOOLEAN 0x08
#define TY_REF 0x09
#define TY_ANY 0x1F
#define TY_LIST 0x40
#define TY_SET 0x80

typedef struct {
  int32_t t,v;
} Value;

typedef struct {
  uint32_t(*random)(void*,uint32_t);
  void*random_v;
} Callbacks;

typedef struct {
  uint8_t team;
} PlayerRules;

typedef struct {
  uint8_t vis,flag,type;
  Value value;
} VarRules;

typedef struct Mahjong_Rules {
  int8_t loaded;
  Callbacks call;
  uint16_t ndeck;
  uint16_t*deck;
  uint8_t ngvar;
  VarRules*gvar;
  uint8_t npvar;
  VarRules*pvar;
  uint8_t nplayer;
  PlayerRules player[4];
  uint8_t handsize;
  uint8_t visibility[VIS_NI];
  uint8_t show;
  uint8_t rounds[32];
} Rules;

typedef struct Mahjong_Game {
  int8_t init,status;
  Rules*rules;
  Callbacks call;
} Game;

