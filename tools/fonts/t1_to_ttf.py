import sys
from fontTools.t1Lib import T1Font
from fontTools.agl import toUnicode
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen
src,dst,family,style=sys.argv[1:5]
f=T1Font(src); f.parse(); gs=f.getGlyphSet()
order=['.notdef']+[n for n in gs.keys() if n!='.notdef']
glyphs={}; metrics={}; cmap={}
for name in order:
    g=gs[name]
    pen=TTGlyphPen(gs)
    g.draw(Cu2QuPen(pen,1.0,reverse_direction=True))
    tt=pen.glyph()
    glyphs[name]=tt
    metrics[name]=(int(g.width),0)
    if name!='.notdef':
        try:
            u=toUnicode(name)
            if len(u)==1: cmap[ord(u)]=name
        except Exception: pass
# ASCII apostrophe and grave come from quotesingle / grave when present, else the Standard-encoding quotes
if 0x27 not in cmap and 'quotesingle' in gs: cmap[0x27]='quotesingle'
if 0x27 not in cmap: cmap[0x27]='quoteright'
if 0x60 not in cmap and 'quoteleft' in gs: cmap[0x60]='quoteleft'
fb=FontBuilder(1000,isTTF=True)
fb.setupGlyphOrder(order); fb.setupCharacterMap(cmap); fb.setupGlyf(glyphs)
tmp={}
for n in order:
    gl=glyphs[n]; gl.recalcBounds(fb.font['glyf']) if hasattr(gl,'recalcBounds') else None
    tmp[n]=(metrics[n][0], getattr(gl,'xMin',0) or 0)
fb.setupHorizontalMetrics(tmp)
fb.setupHorizontalHeader(ascent=740,descent=-240)
fb.setupNameTable({'familyName':family,'styleName':style})
fb.setupOS2(sTypoAscender=740,sTypoDescender=-240,usWinAscent=800,usWinDescent=260)
fb.setupPost(); fb.save(dst)
print('wrote',dst,len(order),'glyphs',len(cmap),'codepoints')
