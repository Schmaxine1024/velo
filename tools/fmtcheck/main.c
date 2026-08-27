#include "fmt.h"
int main(void) {
  char b[32];
  int dist[] = {0, 5, 999, 1000, 1609, 12340, 99999, 123456, 500000};
  for (unsigned i=0;i<sizeof(dist)/sizeof(*dist);i++){
    fmt_distance(b,sizeof b,dist[i],false); printf("dist,%d,metric,%s\n",dist[i],b);
    fmt_distance(b,sizeof b,dist[i],true);  printf("dist,%d,imperial,%s\n",dist[i],b);
  }
  int spd[] = {0, 1, 100, 1005, 1000, 833, 2777, 65535};
  for (unsigned i=0;i<sizeof(spd)/sizeof(*spd);i++){
    fmt_speed(b,sizeof b,spd[i],false); printf("speed,%d,metric,%s\n",spd[i],b);
    fmt_speed(b,sizeof b,spd[i],true);  printf("speed,%d,imperial,%s\n",spd[i],b);
  }
  int asc[] = {0, 1, 100, 500, 1000, 65535};
  for (unsigned i=0;i<sizeof(asc)/sizeof(*asc);i++){
    fmt_ascent(b,sizeof b,asc[i],false); printf("ascent,%d,metric,%s\n",asc[i],b);
    fmt_ascent(b,sizeof b,asc[i],true);  printf("ascent,%d,imperial,%s\n",asc[i],b);
  }
  unsigned dur[] = {0, 59, 60, 599, 3599, 3600, 9912, 86399};
  for (unsigned i=0;i<sizeof(dur)/sizeof(*dur);i++){
    fmt_duration(b,sizeof b,dur[i]); printf("dur,%u,-,%s\n",dur[i],b);
  }
  return 0;
}
