/* Hand-fitted 3 x 5 device-pixel glyphs for overview labels. The counters
   stay open at one pixel per cell, where shrinking the 5 x 7 font cannot. */
#define TINY_GLYPH(a,b,c,d,e) ((a)<<12 | (b)<<9 | (c)<<6 | (d)<<3 | (e))
static uint16_t canvas_tiny_glyph(unsigned char value)
{
    static const uint16_t glyphs[128]={
        ['A']=TINY_GLYPH(2,5,7,5,5),['B']=TINY_GLYPH(6,5,6,5,6),
        ['C']=TINY_GLYPH(3,4,4,4,3),['D']=TINY_GLYPH(6,5,5,5,6),
        ['E']=TINY_GLYPH(7,4,6,4,7),['F']=TINY_GLYPH(7,4,6,4,4),
        ['G']=TINY_GLYPH(3,4,5,5,3),['H']=TINY_GLYPH(5,5,7,5,5),
        ['I']=TINY_GLYPH(7,2,2,2,7),['J']=TINY_GLYPH(1,1,1,5,2),
        ['K']=TINY_GLYPH(5,5,6,5,5),['L']=TINY_GLYPH(4,4,4,4,7),
        ['M']=TINY_GLYPH(5,7,7,5,5),['N']=TINY_GLYPH(5,7,7,7,5),
        ['O']=TINY_GLYPH(2,5,5,5,2),['P']=TINY_GLYPH(6,5,6,4,4),
        ['Q']=TINY_GLYPH(2,5,5,7,3),['R']=TINY_GLYPH(6,5,6,5,5),
        ['S']=TINY_GLYPH(3,4,2,1,6),['T']=TINY_GLYPH(7,2,2,2,2),
        ['U']=TINY_GLYPH(5,5,5,5,7),['V']=TINY_GLYPH(5,5,5,5,2),
        ['W']=TINY_GLYPH(5,5,7,7,5),['X']=TINY_GLYPH(5,5,2,5,5),
        ['Y']=TINY_GLYPH(5,5,2,2,2),['Z']=TINY_GLYPH(7,1,2,4,7),
        ['0']=TINY_GLYPH(7,5,5,5,7),['1']=TINY_GLYPH(2,6,2,2,7),
        ['2']=TINY_GLYPH(6,1,2,4,7),['3']=TINY_GLYPH(6,1,2,1,6),
        ['4']=TINY_GLYPH(5,5,7,1,1),['5']=TINY_GLYPH(7,4,6,1,6),
        ['6']=TINY_GLYPH(3,4,6,5,2),['7']=TINY_GLYPH(7,1,2,2,2),
        ['8']=TINY_GLYPH(2,5,2,5,2),['9']=TINY_GLYPH(2,5,3,1,6),
        ['.']=TINY_GLYPH(0,0,0,0,2),[',']=TINY_GLYPH(0,0,0,2,4),
        [':']=TINY_GLYPH(0,2,0,2,0),['/']=TINY_GLYPH(1,1,2,4,4),
        ['-']=TINY_GLYPH(0,0,7,0,0),['_']=TINY_GLYPH(0,0,0,0,7),
        ['+']=TINY_GLYPH(0,2,7,2,0),['#']=TINY_GLYPH(5,7,5,7,5),
        ['(']=TINY_GLYPH(1,2,2,2,1),[')']=TINY_GLYPH(4,2,2,2,4),
        ['&']=TINY_GLYPH(2,5,2,5,3),['!']=TINY_GLYPH(2,2,2,0,2),
        ['?']=TINY_GLYPH(6,1,2,0,2)
    };
    if(value>='a' && value<='z')value=(unsigned char)(value-'a'+'A');
    if(value==' ')return 0;
    return value<128 && glyphs[value]?glyphs[value]:glyphs['?'];
}
#undef TINY_GLYPH

typedef struct {uint32_t *pixels;int stride,left,top,right,bottom;} CanvasTextSurface;
static void canvas_tiny_text(const CanvasTextSurface *s,int64_t x,int64_t y,int pixel,int limit,
    const char *text,uint32_t ink)
{
    if(y+5*pixel<=s->top || y>=s->bottom)return;
    for(int i=0;i<limit && *text && x<s->right;++i,++text,x+=4*pixel) {
        if(x+3*pixel<=s->left)continue;
        unsigned char letter=(i==limit-1 && i>=3 && text[1])?'.':(unsigned char)*text;
        uint16_t bits=canvas_tiny_glyph(letter);
        for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(bits&(1u<<(14-row*3-col))) {
            int64_t px=x+col*pixel,py=y+row*pixel;
            int64_t left=px<s->left?s->left:px,top=py<s->top?s->top:py;
            int64_t right=px+pixel>s->right?s->right:px+pixel,bottom=py+pixel>s->bottom?s->bottom:py+pixel;
            for(int64_t yy=top;yy<bottom;++yy)for(int64_t xx=left;xx<right;++xx)
                s->pixels[(size_t)yy*s->stride+xx]=ink;
        }
    }
}
