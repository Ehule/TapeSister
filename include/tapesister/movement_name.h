#ifndef TAPESISTER_MOVEMENT_NAME_H
#define TAPESISTER_MOVEMENT_NAME_H
#include <stddef.h>
#include <string.h>

/* Match a whole leading movement name, never a word buried in a layer name.
   The returned suffix includes its delimiter so renaming preserves punctuation. */
static inline const char *ts_movement_suffix(const char *name,const char *movement)
{
    if(!name || !movement || !*movement)return NULL;
    size_t n=strlen(movement);
    if(strncmp(name,movement,n))return NULL;
    const char *p=name+n;
    if(!*p || *p==' ' || *p==':' || *p=='|' || *p=='/' || *p=='_')return p;
    return NULL;
}
static inline const char *ts_movement_layer(const char *name,const char *movement)
{
    const char *p=ts_movement_suffix(name,movement);
    if(!p)return name;
    while(*p==' ' || *p==':' || *p=='|' || *p=='/' || *p=='_' || *p=='-')++p;
    return *p?p:name;
}
#endif
