kwmake.c -> kwmake : bash kwmake.c
kwmake rules.c -> keyword.inc : ./kwmake < rules.c > keyword.inc
rules.c keyword.inc internal.h mahjong.h -> rules.o : bash rules.c
rules.o mahjong.h -> mahjong.a : ar rcDs mahjong.a rules.o
simple.c mahjong.a -> a.out : bash simple.c
a.out -> $ : true
