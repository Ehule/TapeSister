#ifndef TS_TEST_LEGACY_FIXTURE_H
#define TS_TEST_LEGACY_FIXTURE_H
/* Test-only downgrade for the single occupied slot-0, zero-history fixtures.
   Older files omit the route extensions and TSR38's final 108-byte EQ record.
   The last 68 bytes are two zero history counts and 15 empty bank slots. */
static int test_write_legacy_single_tile(const char *input,const char *output,int version)
{
    FILE *in=fopen(input,"rb"),*out=NULL;unsigned char *data=NULL;int ok=0;
    size_t prefix=version==31?64:version==34?92:version==35?112:0;
    if(!in || !prefix)goto done;
    if(fseek(in,0,SEEK_END))goto done;
    long bytes=ftell(in);if(bytes<352 || fseek(in,0,SEEK_SET))goto done;
    size_t size=(size_t)bytes;data=malloc(size);if(!data || fread(data,1,size,in)!=size)goto done;
    if(memcmp(data,"TSR38\r\n\032",8))goto done;
    for(size_t i=size-68;i<size;++i)if(data[i])goto done;
    data[4]=(unsigned char)('0'+version%10);out=fopen(output,"wb");if(!out)goto done;
    ok=fwrite(data,1,prefix,out)==prefix &&
       fwrite(data+176,1,size-352,out)==size-352 &&
       fwrite(data+size-68,1,68,out)==68;
 done:
    if(out && fclose(out))ok=0;
    if(in)fclose(in);
    free(data);return ok;
}
#endif
