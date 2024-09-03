
#include <stdint.h>
#include <stdio.h>

typedef struct Mahjong_Game Mahjong_Game;
typedef struct Mahjong_Language Mahjong_Language;
typedef struct Mahjong_Rules Mahjong_Rules;

typedef struct Mahjong_LoadOption {
  // empty
} Mahjong_LoadOption;

int mahjong_load_rules(Mahjong_Rules*ru,FILE*inf,FILE*errors,Mahjong_LoadOption*option);
void mahjong_reset_rules(Mahjong_Rules*ru);
void mahjong_destroy_rules(Mahjong_Rules*ru);
Mahjong_Rules*mahjong_create_rules(void);
void mahjong_dump_rules(Mahjong_Rules*rul,FILE*fp);

