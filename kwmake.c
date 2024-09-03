#if 0
gcc -s -O2 -o ./kwmake kwmake.c
exit
#endif

#include <err.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const nch[256]={
  ['A' ... 'Z']=1,
  ['a' ... 'z']=1,
  ['0' ... '9']=1,
  ['_']=1,
};

static void*root;
static char buf[32];
static char*form;
static int total=0;

static void add_node(void) {
  char**x=tsearch(buf,&root,(void*)strcmp);
  if(!x) err(1,"Allocation failed");
  if(*x==buf) total++,*x=strdup(buf);
  if(!*x) err(1,"Allocation failed");
}

static void action(const void*n,const VISIT w,const int d) {
  if(w==postorder || w==leaf) printf(form,*(char**)n);
}

int main(int argc,char**argv) {
  int n=0;
  int c;
  while((c=getchar())>0) {
    c&=255;
    switch(n) {
      case -1:
        if(!nch[c]) n=0;
        break;
      case 0 ... 2:
        if(c=="KW_"[n]) n++; else if(nch[c]) n=-1; else n=0;
        break;
      case 3 ... 30:
        if(nch[c]) {
          buf[n++-3]=c;
        } else {
          buf[n-3]=0;
          add_node();
          n=0;
        }
        break;
      case 31:
        buf[28]=0;
        errx(1,"Too long keyword: KW_%s",buf);
        break;
    }
  }
  puts("// Auto-generated; do not modify (see kwmake.c)");
  printf("#define N_KEYWORDS %d\n",total);
  puts("enum{");
  form="KW_%s,\n";
  twalk(root,action);
  puts("};");
  printf("static const char*const keywords[%d]={\n",total);
  form="\"%s\",\n";
  twalk(root,action);
  puts("};");
  return 0;
}
