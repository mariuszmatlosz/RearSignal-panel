"""Render message artwork to GIF/PNG and native-size LVGL images. Requires Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps
import json, math
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'firmware/remote'; ASSETS=DEST/'assets'
FONT=ASSETS/'Roboto.ttf'
W,H=96,16
RED='#ff3030'; WHITE='#ffffff'

def text_fit(im,text,rect,color,size=12):
    x,y,w,h=rect
    for px in range(size,5,-1):
        font=ImageFont.truetype(str(FONT),px)
        bounds=font.getbbox(text)
        if bounds[2]-bounds[0]<=w and bounds[3]-bounds[1]<=h:break
    d=ImageDraw.Draw(im); bounds=d.textbbox((0,0),text,font=font)
    d.text((x+(w-(bounds[2]-bounds[0]))//2-bounds[0],y+(h-(bounds[3]-bounds[1]))//2-bounds[1]),text,font=font,fill=color)

def car(d,x,y,color):
    d.line((x,y+4,x+3,y,x+9,y,x+12,y+4),fill=color,width=1)
    d.rectangle((x,y+4,x+14,y+9),outline=color)
    d.rectangle((x+2,y+10,x+4,y+11),fill=color);d.rectangle((x+10,y+10,x+12,y+11),fill=color)

def headlamp(d,color):
    d.arc((2,4,11,13),85,275,fill=color,width=2)
    d.line((7,4,9,13),fill=color,width=1)
    for y in (3,7,11):d.line((10,y,16,y+3),fill=color)

def triangle(d,cx,color):
    d.line((cx,1,cx-8,14,cx+8,14,cx,1),fill=color,width=1)
    d.line((cx,5,cx,9),fill=color,width=1);d.point((cx,12),fill=color)

def artwork(spec,phase=0,page=None):
    im=Image.new('RGB',(W,H));d=ImageDraw.Draw(im);kind=spec['kind'];color=spec['color'];pages=spec['pages']
    if kind=='speed':
        for cx in (17,48,79):
            text_fit(im,pages[0],(cx-6,4,12,9),WHITE,9)
            if phase>=1:d.arc((cx-7,0,cx+7,14),-90,-90+min(359,phase*120),fill=RED,width=1)
    elif kind=='hazard':
        for cx in (17,48,79):triangle(d,cx,color)
    elif kind=='rain':
        headlamp(d,color);text_fit(im,page or pages[0],(20,0,76,16),color,11)
    elif kind=='thumb':
        # Original pixel-art thumbs-up: thumb rises from a fist.
        top=(7,4,1,1)[phase%4]
        d.polygon([(3,8),(6,8),(8,top),(11,top),(11,7),(16,7),(16,13),(13,15),(6,15),(6,10),(3,10)],fill=color)
        d.rectangle((1,8,4,15),fill=color);text_fit(im,pages[0],(22,0,74,16),color,14)
    elif kind=='brakes':
        # Two cars approach on the same lane; the left car reaches the right one.
        car(d,48+phase*8,3,WHITE);car(d,81,3,color)
        text_fit(im,'MAM DOBRE',(0,0,46,7),color,7);text_fit(im,'HAMULCE',(0,8,46,8),color,8)
    elif kind=='pedestrians':
        d.line((9,0,0,15,18,15,9,0),fill=color)
        d.ellipse((8,4,10,6),fill=color);d.line((9,7,8,10,5,13),fill=color);d.line((8,10,12,13),fill=color);d.line((6,9,9,7,12,9),fill=color)
        for x in (2,7,12):d.line((x,14,x+3,14),fill=color)
        text_fit(im,' '.join(pages),(23,0,73,16),color,11)
    elif kind=='left':
        x=4-phase%4
        d.line((x+8,2,x+1,8,x+8,14),fill=color,width=2);d.line((x+2,8,19,8),fill=color,width=2)
        text_fit(im,pages[0],(23,0,73,16),color,11)
    elif kind=='ford':
        logo=Image.open(ASSETS/'ford.png').convert('RGB');logo=ImageOps.contain(logo,(48,16),Image.Resampling.LANCZOS);im.paste(logo,((W-logo.width)//2,0))
    elif kind=='honda':
        logo=Image.open(ASSETS/'honda.png').convert('RGB');logo=ImageOps.contain(logo.crop((0,0,96,72)),(20,16),Image.Resampling.LANCZOS);im.paste(logo,(4,0));text_fit(im,'HONDA',(27,0,65,16),WHITE,14)
    else:
        if page is not None:
            if '\n' in page:
                for i,line in enumerate(page.splitlines()[:2]):text_fit(im,line,(0,i*8,96,8),color,9)
            else:text_fit(im,page,(0,0,96,16),color,12)
        elif len(pages)==1:text_fit(im,pages[0],(0,0,96,16),color,13)
        else:
            for i,line in enumerate(pages[:2]):text_fit(im,line,(0,i*8,96,8),color,9)
    return im

def timeline(spec):
    if not spec['animated']:return [artwork(spec)],[1000]
    if spec['kind'] in ('rain','text'):
        frames=[];times=[]
        for page in spec['pages']:
            full=artwork(spec,page=page);frames.append(full);times.append(850)
            # Text moves out; rain icon remains visible through every transition.
            for dy in (4,8,12):
                slide=Image.new('RGB',(96,16));slide.paste(full,(0,-dy))
                if spec['kind']=='rain':
                    ImageDraw.Draw(slide).rectangle((0,0,19,15),fill='black');headlamp(ImageDraw.Draw(slide),spec['color'])
                frames.append(slide);times.append(90)
        return frames,times
    return [artwork(spec,i) for i in range(4)],[240,240,240,650]

def rgb565(im):return [((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in im.convert('RGB').get_flattened_data()]

def generate():
    specs=json.loads((ASSETS/'messages.json').read_text());assert len(specs)==16
    arrays=[];entries=[];sheets=[]
    def array(name,data,typ='uint8_t'):
        arrays.append(f'static const {typ} {name}[] PROGMEM = {{'+','.join(str(x) for x in data)+'};\n')
    for i,spec in enumerate(specs):
        frames,times=timeline(spec)
        file=ASSETS/f'message-{i+1:02}.gif'
        frames[0].save(file,save_all=True,append_images=frames[1:],duration=times,loop=0,optimize=True,disposal=2)
        # Save actual bytes and timings after GIF optimization/coalescing.
        gif=Image.open(file);total=sum(gif.seek(f) or gif.info['duration'] for f in range(gif.n_frames));array(f'gif{i}',file.read_bytes())
        pointers=[]
        for sample in range(4):
            when=total*sample/4;elapsed=0;chosen=0
            for frame,ms in enumerate(times):
                if elapsed<=when:chosen=frame
                elapsed+=ms
            if not spec['animated'] and sample:pointers.append(f'preview{i}_0');continue
            preview=frames[chosen].resize((192,32),Image.Resampling.NEAREST);array(f'preview{i}_{sample}',rgb565(preview),'uint16_t');pointers.append(f'preview{i}_{sample}')
        frames[0].save(ASSETS/f'message-{i+1:02}.png');sheets.append(frames[0])
        entries.append('{gif%d,sizeof(gif%d),{%s},%d,%s}'%(i,i,','.join(pointers),max(100,total//4),'true' if spec.get('unlimited') else 'false'))
    array('fordPixels',rgb565(Image.open(ASSETS/'ford.png')),'uint16_t')
    (DEST/'assets.h').write_text('#pragma once\n#include <Arduino.h>\nconstexpr int kAnimationCount=16;\nstruct AnimationAsset {const uint8_t *gif; size_t length; const uint16_t *frames[4]; uint16_t previewStepMs; bool unlimited;};\n'+''.join(arrays)+'static const AnimationAsset animations[] = {'+','.join(entries)+'};\n')
    # Contact sheet is a development artifact, not the LCD UI.
    sheet=Image.new('RGB',(384,16*72))
    for i,im in enumerate(sheets):sheet.paste(im.resize((384,64),Image.Resampling.NEAREST),(0,i*72))
    sheet.save(ASSETS/'contact-sheet.png')
    print('Generated 16 message assets; GIF sizes:',[ (ASSETS/f'message-{i+1:02}.gif').stat().st_size for i in range(16)])
if __name__=='__main__':generate()
