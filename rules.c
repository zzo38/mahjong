#if 0
gcc -g -O0 -c rules.c
exit
#endif

#define _GNU_SOURCE
#include <search.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "keyword.inc"

#define TOK_SPACE 1
#define TOK_DELIM 2
#define TOK_WORD 3
#define TOK_SIGIL 4
#define TOK_COMMENT 5

#define TOK_DICE 'd'
#define TOK_EOF 'E'
#define TOK_IDTILE '\''
#define TOK_NUMBER '0'
#define TOK_TILEFLAG '^'
#define TOK_GLOBALVAR '@'
#define TOK_PLAYERVAR '%'

static const uint8_t catcode[128]={
  // Spaces
  [' ']=1, ['\t']=1, ['\r']=1, ['\n']=1, ['\f']=1, ['\v']=1,
  // Delimiters
  ['<']=2, ['>']=2, ['[']=2, [']']=2, ['(']=2, [')']=2, ['{']=2, ['}']=2,
  ['=']=2, [',']=2,
  // Words
  ['0' ... '9']=3, ['A' ... 'Z']=3, ['a' ... 'z']=3,
  ['_']=3, ['-']=3, ['+']=3, ['.']=3,
  // Sigils
  ['~']=4, ['!']=4, ['@']=4, ['#']=4, ['$']=4, ['%']=4, ['^']=4,
  ['&']=4, ['*']=4, ['/']=4, ['?']=4, [':']=4, ['|']=4, ['"']=4,
  ['\'']=4, ['\\']=4,
  // Comments
  [';']=5,
};

static const int8_t casefold[128]={
  ['a' ... 'z']='A'-'a',
  ['-']='_'-'-',
  ['.']='x'-'.',
};

typedef struct {
  char*name;
  uint16_t value;
} Name;

typedef struct {
  uint16_t mask,value;
} Quality;

static unsigned int linenum;
static uint8_t tokent;
static int32_t tokenv;
static char tokenstr[64];
static Name*tokennam;
static char defining;
static void*root;
static FILE*infile;
static FILE*errfile;
static jmp_buf renv;
static Rules*ru;
static uint8_t nexttileflag;

#define ReturnToken(tt,vv) do{ tokent=(tt); tokenv=(vv); return; }while(0)
#define Error(...) do{ if(errfile) { fprintf(errfile,"Error on line %u:  ",linenum); fprintf(errfile,__VA_ARGS__); fputc('\n',errfile); } longjmp(renv,1); }while(0)

static name_compare(const void*a,const void*b) {
  const Name*x=a;
  const Name*y=b;
  return strcmp(x->name,y->name);
}

static Name*find_name(const char*name) {
  Name a={(char*)name,0};
  void*b=tsearch(&a,&root,name_compare);
  Name*c;
  if(!b) return 0;
  c=*(Name**)b;
  if(c==&a && (*(Name**)b=c=malloc(sizeof(Name))) && (a.name=strdup(name))) {
    *c=a;
    if(!defining) Error("Undefined name: %s",name);
    defining=3;
  } else {
    defining=2;
  }
  return c;
}

static int kw_compare(const void*a,const void*b) {
  const char*const*x=a;
  const char*const*y=b;
  return strcmp(*x,*y);
}

static uint8_t parse_idtile(void) {
  uint8_t n,s;
  if(tokenstr[1] && !tokenstr[2]) {
    switch(tokenstr[1]) {
      case 'C': return 0x40;
      case 'E': return 0x7C;
      case 'F': return 0x60;
      case 'N': return 0x7F;
      case 'P': return 0x50;
      case 'S': return 0x7D;
      case 'W': return 0x7E;
    }
  } else if(tokenstr[1]>='0' && tokenstr[1]<='9') {
    if(tokenstr[1]=='1' && tokenstr[2]>='0' && tokenstr[2]<'6') {
      n=tokenstr[2]+10-'0';
      s=tokenstr[3];
      if(tokenstr[4]) goto bad;
    } else {
      n=tokenstr[1]-'0';
      s=tokenstr[2];
      if(tokenstr[3]) goto bad;
    }
    switch(s) {
      case 'M': return n+0x00;
      case 'P': return n+0x10;
      case 'S': return n+0x20;
    }
  } else if(tokenstr[1]=='X' && tokenstr[2]>='1' && tokenstr[2]<='4' && tokenstr[3] && !tokenstr[4]) {
    switch(tokenstr[3]) {
      case 'F': return tokenstr[2]+0x80-'1';
      case 'K': return tokenstr[2]+0x88-'1';
      case 'S': return tokenstr[2]+0x84-'1';
      case 'Q': return tokenstr[2]+0x8C-'1';
    }
  }
  bad: Error("Improper token");
}

static void nexttok(void) {
  static const char*const kp=tokenstr;
  const char*const*kw;
  char*s;
  Name*na;
  int c,i,t;
  restart:
  c=fgetc(infile);
  if(c=='\n') ++linenum;
  if(c==EOF) ReturnToken(TOK_EOF,0);
  if(c&~0x7F) Error("Invalid character");
  t=catcode[c];
  c+=casefold[c];
  switch(t) {
    case TOK_SPACE: goto restart;
    case TOK_DELIM: ReturnToken(c,0);
    case TOK_WORD:
      *tokenstr=c;
      for(i=1;i<63;i++) {
        c=fgetc(infile);
        if(c==EOF) break;
        if(c&~0x7F) Error("Invalid character");
        if(catcode[c]!=TOK_WORD) break;
        tokenstr[i]=c+casefold[c];
      }
      tokenstr[i]=0;
      if(c!=EOF) ungetc(c,infile);
      if(*tokenstr=='0' && tokenstr[1]=='X') {
        // Hexadecimal
        tokenv=strtol(tokenstr+2,&s,16);
        if(!*s) ReturnToken(TOK_NUMBER,tokenv);
      } else if(*tokenstr=='+' || *tokenstr=='-' || (*tokenstr>='0' && *tokenstr<='9')) {
        // Decimal
        tokenv=strtol(tokenstr,&s,10);
        if(!*s) ReturnToken(TOK_NUMBER,tokenv);
        if(*s=='D' && !(tokenv&~255) && s[1]>'0' && s[1]<='9') {
          tokenv|=strtol(s+1,&s,10)<<8;
          if(!*s) ReturnToken(TOK_DICE,tokenv);
        }
      }
      kw=bsearch(kw=&kp,keywords,N_KEYWORDS,sizeof(*keywords),kw_compare);
      if(!kw) Error("Unrecognized word \"%s\"",tokenstr);
      ReturnToken(TOK_WORD,kw-keywords);
    case TOK_SIGIL:
      *tokenstr=tokent=c;
      for(i=1;i<63;i++) {
        c=fgetc(infile);
        if(c==EOF) break;
        if(c&~0x7F) Error("Invalid character");
        if(catcode[c]!=TOK_WORD) break;
        tokenstr[i]=c+casefold[c];
      }
      tokenstr[i]=0;
      if(c!=EOF) ungetc(c,infile);
      switch(*tokenstr) {
        case '\'':
          ReturnToken(TOK_IDTILE,parse_idtile());
        default:
          tokennam=na=find_name(tokenstr);
          if(!na) Error("Unexpected error");
          ReturnToken(tokent,na->value);
      }
    case TOK_COMMENT:
      for(;;) {
        c=fgetc(infile);
        if(c==EOF) ReturnToken(TOK_EOF,0);
        if(c=='\n') break;
      }
      ++linenum;
      goto restart;
    default: Error("Invalid character");
  }
}

static inline void add_group_to_deck(uint16_t ng,Quality*g) {
  uint16_t n;
  uint16_t*d;
  if(ng+ru->ndeck>0xFFFE) Error("Too many tiles");
  d=realloc(ru->deck,(ng+ru->ndeck)*sizeof(uint16_t));
  if(!d) {
    free(ru->deck);
    ru->deck=0;
    ru->ndeck=0;
    Error("Allocation failed");
  }
  ru->deck=d;
  d+=ru->ndeck;
  ru->ndeck+=ng;
  for(n=0;n<ng;n++) {
    if((g[n].mask&0xFF)!=0xFF) Error("Tile underspecification");
    d[n]=g[n].mask&g[n].value;
  }
}

static inline void subtract_group_from_deck(uint16_t ng,Quality*g) {
  uint16_t n,m;
  for(m=0;m<ng && ru->ndeck;m++) {
    for(n=0;n<ru->ndeck;n++) if((ru->deck[n]&g[m].mask)==g[m].value) {
      ru->deck[n]=ru->deck[--ru->ndeck];
      break;
    }
  }
}

static inline void subtract_all_group_from_deck(uint16_t ng,Quality*g) {
  uint16_t n,m;
  for(n=0;n<ru->ndeck;) {
    for(m=0;m<ng;m++) if((ru->deck[n]&g[m].mask)==g[m].value) goto found;
    n++; continue;
    found:
    ru->deck[n]=ru->deck[--ru->ndeck];
  }
}

static uint16_t multiply_tile_by(uint16_t t,Quality q) {
  if(q.mask&0xFF) Error("Improper tile modifier in TIMES clause");
  return (t&~q.mask)|(q.value&q.mask);
}

static inline void multiply_group_by_deck(uint16_t ng,Quality*g) {
  int32_t n,m;
  uint16_t*d;
  if(!ru->ndeck || !ng) Error("Unable to modify with nonexistent tiles");
  if(ng>0x8000 || ru->ndeck>0x8000 || ng*ru->ndeck>=0xFFFE) Error("Too many tiles");
  d=realloc(ru->deck,(ng*ru->ndeck)*sizeof(uint16_t));
  if(!d) Error("Allocation failed");
  ru->deck=d;
  for(n=0;n<ru->ndeck;n++) {
    for(m=ng-1;m>=0;m--) {
      d[m*ru->ndeck+n]=multiply_tile_by(d[n],g[m]);
      if(!m) break;
    }
  }
  ru->ndeck*=ng;
}

static Quality quality_by_token(void) {
  if(tokent==TOK_IDTILE) {
    return (Quality){0x00FF,tokenv};
  } else if(tokent==TOK_WORD) {
    switch(tokenv) {
      case KW_MANZU: return (Quality){0x00F0,0x00};
      case KW_PINZU: return (Quality){0x00F0,0x10};
      case KW_SOUZU: return (Quality){0x00F0,0x20};
      case KW_FLOWERS: return (Quality){0x00C0,0x80};
      case KW_CUSTOM: return (Quality){0x00C0,0xC0};
      default: goto bad;
    }
  } else if(tokent==TOK_NUMBER) {
    if(tokenv&~15) goto bad;
    return (Quality){0x00CF,tokenv};
  } else if(tokent==TOK_TILEFLAG) {
    return (Quality){tokenv&0xFF00,(tokenv&0xFF)<<8};
  } else {
    bad: Error("Improper tile or tile modifier specifications");
  }
}

static inline void modify_quality_by(Quality*q,Quality z) {
  if(q->mask&z.mask&(q->value^z.value)) Error("Tile overspecification");
  q->value|=z.value&z.mask;
  q->mask|=z.mask;
}

static inline void modify_all_deck_by_quality(uint16_t ng,const Quality*g,Quality z) {
  uint16_t n,m;
  for(n=0;n<ru->ndeck;n++) {
    for(m=0;m<ng;m++) if((ru->deck[n]&g[m].mask)==g[m].value) goto found;
    continue;
    found:
    ru->deck[n]&=~z.mask;
    ru->deck[n]|=z.mask&z.value;
  }
}

static inline void modify_deck_by_quality(uint16_t ng,const Quality*g,Quality z) {
  uint16_t n,m;
  for(m=0;m<ng;m++) {
    for(n=0;n<ru->ndeck;n++) if((ru->deck[n]&g[m].mask)==g[m].value) {
      ru->deck[n]&=~z.mask;
      ru->deck[n]|=z.mask&z.value;
      break;
    }
  }
}

static void define_deck(void) {
  int32_t i,j;
  int32_t w=KW_PLUS;
  char al=0;
  uint16_t ng=0;
  Quality*g=0;
  uint8_t nq;
  Quality q[200];
  Quality z;
  defining=0;
  if(ru->ndeck) Error("Extra TILES block");
  loop:
  nexttok();
  if(tokent==TOK_WORD) {
    switch(tokenv) {
      case KW_PLUS: case KW_MINUS: case KW_TIMES: case KW_MODIFY: case KW_AND: endclause:
        if(ng) {
          switch(w) {
            case KW_PLUS: add_group_to_deck(ng,g); break;
            case KW_TIMES: multiply_group_by_deck(ng,g); break;
            case KW_MINUS: if(al) subtract_all_group_from_deck(ng,g); else subtract_group_from_deck(ng,g); break;
            case KW_BY: Error("Expected next clause of end of TILES block"); break;
            default: Error("MODIFY without BY");
          }
          ng=0;
        }
        if(tokent!=TOK_WORD) goto done;
        if(tokenv!=KW_AND) w=tokenv,al=0;
        break;
      case KW_ALL:
        if(al || (w!=KW_MINUS && w!=KW_MODIFY)) Error("Improper use of ALL");
        al=1;
        break;
      case KW_BY:
        if(w!=KW_MODIFY) Error("Improper use of BY");
        w=KW_BY;
        nexttok();
        if(tokent=='(') {
          z.mask=z.value=0;
          for(;;) {
            nexttok();
            if(tokent==')') break;
            if(tokent!=TOK_TILEFLAG) Error("Expected tile flag");
            modify_quality_by(&z,quality_by_token());
          }
        } else if(tokent==TOK_TILEFLAG) {
          z=quality_by_token();
        } else {
          Error("Expected tile flag or () block of tile flags");
        }
        if(ng) {
          if(al) modify_all_deck_by_quality(ng,g,z); else modify_deck_by_quality(ng,g,z);
        }
        ng=0;
        break;
      default: Error("Unexpected token: %s",tokenstr);
    }
  } else if(tokent==TOK_NUMBER) {
    if(tokenv<1) Error("Multiplier too small");
    if(!ng) {
      if(tokenv>255) Error("Multiplier too big");
      g=realloc(g,tokenv*sizeof(Quality));
      if(!g) Error("Allocation failed");
      ng=tokenv;
      for(i=0;i<ng;i++) g[i].mask=g[i].value=0;
    } else if(tokenv!=1) {
      if(tokenv>255 || (tokenv*(uint32_t)ng)>0xFFFE) Error("Multiplier too big");
      g=realloc(g,ng*tokenv*sizeof(Quality));
      if(!g) Error("Allocation failed");
      for(i=1;i<tokenv;i++) for(j=0;j<ng;j++) g[ng*i+j]=g[j];
      ng*=tokenv;
    }
  } else if(tokent=='(') {
    nq=0;
    for(;;) {
      nexttok();
      if(tokent==')') break;
      if(nq==200) Error("Too many tiles per group in a sub-block of the TILES block");
      if(tokent=='(') {
        q[nq].mask=q[nq].value=0;
        for(;;) {
          nexttok();
          if(tokent==')') break;
          modify_quality_by(q+nq,quality_by_token());
        }
        nq++;
      } else if(tokent==TOK_WORD && tokenv==KW_TO) {
        if(nq!=1) goto improper_to;
        nexttok();
        if(tokent==TOK_NUMBER && tokenv>(q->value&15) && tokenv<16 && q->mask==0xCF && !(q->value&0xC0)) {
          nq=tokenv+1-(q->value&15);
        } else if(tokent==TOK_IDTILE && q->mask==0xFF && tokenv>q->value) {
          if(tokenv&0x40) goto improper_to;
          if((tokenv^q->value)&0xF0) goto improper_to;
          nq=tokenv+1-q->value;
        } else {
          goto improper_to;
        }
        for(i=1;i<nq;i++) q[i]=q[i-1],q[i].value++;
        nexttok();
        if(tokent==')') break;
        improper_to: Error("Improper use of TO");
      } else {
        q[nq++]=quality_by_token();
      }
    }
    if(!nq) Error("Empty group in TILES block");
    if(!ng) {
      ng=1;
      if(!g) {
        g=malloc(sizeof(Quality));
        if(!g) Error("Allocation failed");
      }
      g->mask=g->value=0;
    }
    if(ng*nq>=0xFFFE) Error("Too many tiles");
    g=realloc(g,ng*nq*sizeof(Quality));
    if(!g) Error("Allocation failed");
    for(i=1;i<nq;i++) for(j=0;j<ng;j++) g[i*ng+j]=g[j];
    for(i=0;i<ng*nq;i++) modify_quality_by(g+i,q[i/ng]);
    ng*=nq;
  } else if(tokent==')') {
    goto endclause;
  } else {
    Error("Expected (, ), PLUS, MINUS, TIMES, MODIFY, AND, ALL, BY, or a number");
  }
  goto loop;
  done:
  free(g);
  if(!ru->ndeck) Error("TILES block does not define any tiles");
}

static void set_default_deck(void) {
  int n;
  static const uint8_t d[3*9+7]={
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
    0x40, 0x50, 0x60, 0x7C, 0x7D, 0x7E, 0x7F,
  };
  ru->ndeck=(3*9+7)*4;
  ru->deck=malloc(ru->ndeck*sizeof(uint16_t));
  if(!ru->deck) Error("Allocation failed5");
  for(n=0;n<3*9+7;n++) ru->deck[4*n]=ru->deck[4*n+1]=ru->deck[4*n+2]=ru->deck[4*n+3]=d[n];
}

static void define_tile_flags(void) {
  Name*a[16];
  int b=nexttileflag;
  int n=0;
  int m;
  if(b==8) Error("Too many tile flags");
  loop:
  if(n==16) Error("Too many tile flags per block");
  if(defining!=3) Error("Tile flag %s already defined",tokenstr);
  a[n]=tokennam;
  tokennam->value=n++<<b;
  nexttok();
  if(tokent==TOK_TILEFLAG) goto loop;
  if(tokent!=')') Error("Expected ) or ^");
  if(n<2) Error("Tile flag %s cannot be defined alone",a[0]->name);
  b=(n<=2?1:n<=4?3:n<=8?7:15)<<(b+8);
  for(m=0;m<n;m++) a[m]->value|=b;
  nexttileflag+=(n<=2?1:n<=4?2:n<=8?3:4);
  if(nexttileflag>8) Error("Too many tile flags");
}

static void define_players(void) {
  int i;
  defining=0;
  nexttok();
  if(tokent==TOK_NUMBER) {
    if(tokenv<2 || tokenv>4) Error("Number of players must be 2, 3, or 4");
    ru->nplayer=tokenv;
    nexttok();
    goto end;
  }
  ru->nplayer=0;
  while(tokent=='(') {
    if(ru->nplayer==4) Error("Too many players");
    for(;;) {
      nexttok();
      if(tokent==')') break;
      if(tokent==TOK_WORD) {
        switch(tokenv) {
          case KW_TEAM:
            nexttok();
            if(tokent!=TOK_NUMBER || (tokenv&~3)) Error("Expected number 0 to 3");
            ru->player[ru->nplayer].team=tokenv;
            break;
        }
      } else {
        Error("Syntax error");
      }
    }
    ru->nplayer++;
    nexttok();
  }
  if(ru->nplayer<2) Error("Not enough players");
  end:
  if(tokent!=')') Error("Unterminated PLAYERS block");
  nexttok();
  return;
}

static void define_variables(void) {
  Name*na=tokennam;
  VarRules*vr;
  uint8_t f;
  char g=(tokent==TOK_GLOBALVAR);
  if(defining==2) Error("%s is already defined",tokenstr);
  defining=0;
  if((g?ru->ngvar:ru->npvar)>253) Error("Too many %s variables",g?"global":"player");
  tokennam->value=(g?ru->ngvar++:ru->npvar++);
  if(g) {
    vr=realloc(ru->gvar,ru->ngvar*sizeof(VarRules));
    if(!vr) Error("Allocation failed");
    ru->gvar=vr;
  } else {
    vr=realloc(ru->pvar,ru->npvar*sizeof(VarRules));
    if(!vr) Error("Allocation failed");
    ru->pvar=vr;
  }
  vr+=tokennam->value;
  vr->vis=VIS_V_NONE;
  vr->flag=VF_NULL;
  vr->type=0;
  vr->value.t=TY_NULL;
  vr->value.v=0;
  nexttok();
  if(tokent==TOK_WORD && tokenv==KW_TEAM) {
    if(g) Error("TEAM is not valid for global variables");
    vr->flag|=VF_TEAM;
    nexttok();
  }
  if(tokent==TOK_WORD) switch(tokenv) {
    case KW_PRIVATE: if(g) Error("PRIVATE visibility is not valid for global variables"); vr->vis=VIS_V_OWNER; nexttok(); break;
    case KW_FACING: if(g) Error("FACING visibility is not valid for global variables"); vr->vis=VIS_V_BACK|VIS_V_OWNER; nexttok(); break;
    case KW_PUBLIC: vr->vis=VIS_V_FRONT; nexttok(); break;
    case KW_BACK: vr->vis=VIS_V_BACK; nexttok(); break;
  }
  if(tokent==TOK_WORD && tokenv==KW_LIST) {
    nexttok();
    if(tokent==TOK_WORD && tokenv==KW_OF) nexttok();
    vr->type=TY_LIST;
  } else if(tokent==TOK_WORD && tokenv==KW_SET) {
    nexttok();
    if(tokent==TOK_WORD && tokenv==KW_OF) nexttok();
    vr->type=TY_SET;
  }
  if(tokent==TOK_WORD) switch(tokenv) {
    case KW_ANY: vr->type|=TY_ANY; nexttok(); break;
    case KW_NUMBER: vr->type|=TY_NUMBER; nexttok(); break;
    case KW_TILE: vr->type|=TY_TILE; nexttok(); break;
    case KW_PLAYER: vr->type|=TY_PLAYER; nexttok(); break;
    case KW_DIRECTION: vr->type|=TY_DIR; nexttok(); break;
    case KW_BOOLEAN: vr->type|=TY_BOOLEAN; nexttok(); break;
  }
  if(!(vr->type&TY_ANY)) vr->type|=TY_ANY;
  if((vr->vis&VIS_V_BACK) && (vr->type&TY_ANY)!=TY_TILE) Error("BACK or FACING visibility cannot be used with types other than TILE");
  if(tokent==TOK_WORD) switch(tokenv) {
    case KW_INIT: vr->flag|=VF_INIT; nexttok(); break;
    case KW_GAME: vr->flag|=VF_GAME; nexttok(); break;
    case KW_ROUND: vr->flag|=VF_ROUND; nexttok(); break;
    case KW_DEAL: vr->flag|=VF_DEAL; nexttok(); break;
    case KW_NONREPEAT: vr->flag|=VF_NONREPEAT; nexttok(); break;
  }
  if(vr->flag&0x0F) {
    if(tokent==TOK_WORD && tokenv==KW_NULL) {
      nexttok();
    } else {
      switch(vr->type) {
        case TY_BOOLEAN:
          if(tokent==TOK_NUMBER && !(tokenv&~1)) vr->value.t=TY_BOOLEAN,vr->value.v=tokenv,nexttok();
          else if(tokent==TOK_WORD && tokenv==KW_TRUE) vr->value.t=TY_BOOLEAN,vr->value.v=1,nexttok();
          else if(tokent==TOK_WORD && tokenv==KW_FALSE) vr->value.t=TY_BOOLEAN,vr->value.v=0,nexttok();
          break;
        case TY_NUMBER:
          if(tokent==TOK_NUMBER) vr->value.t=TY_NUMBER,vr->value.v=tokenv,nexttok();
          break;
        case TY_TILE:
          if(tokent==TOK_IDTILE) {
            vr->value.t=TY_TILE;
            vr->value.v=tokenv;
            for(f=0;tokent==TOK_TILEFLAG;) {
              if(f&(tokenv>>8)) Error("Conflicting tile flags in variable definition");
              f|=tokenv>>8;
              vr->value.v|=(tokenv&0xFF)<<8;
              nexttok();
            }
          }
          break;
        case TY_SET: case TY_SET+TY_ANY:
          Error("Improper SET type");
      }
    }
  } else {
    vr->flag|=VF_INIT;
  }
  if(tokent!=')') Error("Improper token in variable definition");
}

static void define_visibility(void) {
  uint8_t c,w;
  defining=0;
  for(;;) {
    nexttok();
    if(tokent==')') break;
    if(tokent!=TOK_WORD) goto bad;
    switch(tokenv) {
      case KW_HAND: c=VIS_I_HAND; w=0; break;
      case KW_WALL: c=VIS_I_LIVE_WALL; w=VIS_V_STACKED; break;
      case KW_DEADWALL: c=VIS_I_DEAD_WALL; w=VIS_V_STACKED; break;
      case KW_KAN: c=VIS_I_KAN; w=VIS_V_MIXED; break;
      case KW_FLOWERS: c=VIS_I_FLOWERS; w=0; break;
      case KW_DORA: c=VIS_I_DORA; w=0; break;
      case KW_URADORA: c=VIS_I_URADORA; w=0; break;
      default: goto bad;
    }
    nexttok();
    if(tokent!=TOK_WORD) goto bad;
    switch(tokenv) {
      case KW_HIDDEN: w=0; break;
      case KW_FRONT: case KW_OPEN: w=VIS_V_FRONT; break;
      case KW_BACK: case KW_CLOSED: w=VIS_V_BACK; break;
      case KW_MIXED: if(w!=VIS_V_MIXED) goto bad; break;
      case KW_STACKED: if(w!=VIS_V_STACKED) goto bad; break;
      default: bad: Error("Unexpected token in VISIBILITY block");
    }
    if(tokenv==KW_STACKED) {
      nexttok();
      if(tokent!=TOK_WORD) goto bad;
      switch(tokenv) {
        case KW_HIDDEN: w=0; break;
        case KW_FRONT: case KW_OPEN: w|=VIS_V_FRONT; break;
        case KW_BACK: case KW_CLOSED: w|=VIS_V_BACK; break;
        default: goto bad;
      }
    }
    ru->visibility[c]=w;
  }
}

static void define_show(void) {
  defining=0;
  while(nexttok(),tokent!=')') {
    if(tokent!=TOK_WORD) Error("Expected word");
    switch(tokenv) {
      case KW_DEALT: ru->show|=SHOW_DEALT; break;
      case KW_TENPAI: ru->show|=SHOW_TENPAI; break;
      case KW_TSUMOGIRI: ru->show|=SHOW_TSUMOGIRI; break;
    }
  }
}

static void load_rules_1(void) {
  while(nexttok(),tokent!=TOK_EOF) {
    if(tokent!='(') Error("Expected ("); //))
    defining=1;
    nexttok();
    if(tokent==TOK_WORD) {
      switch(tokenv) {
        case KW_HAND:
          nexttok();
          if(tokent!=TOK_NUMBER || tokenv<6 || tokenv>30 || tokenv%3==1) Error("Improper hand size");
          ru->handsize=tokenv;
          nexttok();
          if(tokent!=')') Error("Expected end of block");
          break;
        case KW_PLAYERS: define_players(); break;
        case KW_SHOW: define_show(); break;
        case KW_TILES: define_deck(); break;
        case KW_VISIBILITY: define_visibility(); break;
        default: Error("Improper token");
      }
    } else if(tokent==TOK_TILEFLAG) {
      define_tile_flags();
    } else if(tokent==TOK_GLOBALVAR || tokent==TOK_PLAYERVAR) {
      define_variables();
    } else {
      Error("Improper token");
    }
  }
}

int mahjong_load_rules(Rules*rul,FILE*inf,FILE*errors,Mahjong_LoadOption*opt) {
  int i;
  // Check if already loaded or cannot load
  if(!rul || rul->loaded || !inf) {
    if(errors) {
      if(rul && rul->loaded) fprintf(errors,"Rules already loaded\n");
      if(!inf) fprintf(errors,"No rule file\n");
    }
    return -1;
  }
  // Initialize variables
  ru=rul;
  nexttileflag=0;
  // Initialize defaults
  ru->nplayer=4;
  for(i=0;i<4;i++) ru->player[i].team=i;
  ru->handsize=14;
  ru->visibility[VIS_I_HAND]=VIS_V_BACK;
  ru->visibility[VIS_I_LIVE_WALL]=VIS_V_NONE;
  ru->visibility[VIS_I_DEAD_WALL]=VIS_V_NONE;
  ru->visibility[VIS_I_KAN]=VIS_V_MIXED;
  ru->visibility[VIS_I_FLOWERS]=VIS_V_FRONT;
  ru->visibility[VIS_I_DORA]=VIS_V_FRONT;
  ru->visibility[VIS_I_URADORA]=VIS_V_NONE;
  ru->show=0;
  // Load rules from file
  infile=inf;
  errfile=errors;
  linenum=1;
  if(setjmp(renv)) {
    ru->loaded=-1;
    goto end;
  }
  load_rules_1();
  // Initialize defaults
  if(!ru->deck) set_default_deck();
  // Verification
  // (TODO)
  // Reduce memory usage
  ru->deck=realloc(ru->deck,ru->ndeck*sizeof(uint16_t))?:ru->deck;
  // Load flag
  ru->loaded=1;
  end:
  // Move and/or delete name data
  // (TODO)
  // End
  return ru->loaded==1?0:-1;
}

void mahjong_reset_rules(Rules*ru) {
  if(!ru || !ru->loaded) return;
  ru->ndeck=0;
  free(ru->deck);
  ru->deck=0;
  ru->loaded=0;
  ru->ngvar=ru->npvar=0;
  free(ru->gvar);
  free(ru->pvar);
  ru->gvar=ru->pvar=0;
}

void mahjong_destroy_rules(Rules*ru) {
  mahjong_reset_rules(ru);
  free(ru);
}

Rules*mahjong_create_rules(void) {
  Rules*ru=calloc(1,sizeof(Rules));
  return ru;
}

void mahjong_dump_rules(Rules*r,FILE*f) {
  int i;
  if(!f || !r) return;
  fprintf(f,"loaded=%d;\n",r->loaded);
  if(r->loaded!=1) return;
  fprintf(f,"Tile set: (%d)\n ",r->ndeck);
  for(i=0;i<r->ndeck;i++) fprintf(f," %04X",r->deck[i]);
  fputc('\n',f);
  fprintf(f,"Players:\n");
  for(i=0;i<r->nplayer;i++) fprintf(f,"  %d: team=%d\n",i,r->player[i].team);
  fprintf(f,"Player variables:\n");
  for(i=0;i<r->npvar;i++) fprintf(f,"  %d: vis=%X,flag=%X,type=%X; (%lX,%lX)\n",i,r->pvar[i].vis,r->pvar[i].flag,r->pvar[i].type,(long)r->pvar[i].value.t,(long)r->pvar[i].value.v);
  fprintf(f,"Global variables:\n");
  for(i=0;i<r->ngvar;i++) fprintf(f,"  %d: vis=%X,flag=%X,type=%X; (%lX,%lX)\n",i,r->gvar[i].vis,r->gvar[i].flag,r->gvar[i].type,(long)r->gvar[i].value.t,(long)r->gvar[i].value.v);
  fprintf(f,"Hand size: %d\n",ru->handsize);
  fprintf(f,"Visibility:\n ");
  for(i=0;i<VIS_NI;i++) fprintf(f," %02X",r->visibility[i]);
  fputc('\n',f);
  fprintf(f,"Show: %X\n",r->show);
}

