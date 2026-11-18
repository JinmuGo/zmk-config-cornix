// Generate the README preview from the firmware's real pixels and animation timing.
// Run from the repository root: node generate-oled-preview.mjs
// No packages required. The GIF previews the 50x26 character area, not the full OLED.
import fs from 'node:fs';
const root = new URL('./', import.meta.url);
const dir = 'boards/shields/dongle_display/widgets/';
const images = fs.readFileSync(new URL(dir + 'bongo_cat_images.c', root), 'utf8');
const animation = fs.readFileSync(new URL(dir + 'bongo_cat.c', root), 'utf8');
const sprites = {};
for (const m of images.matchAll(/uint8_t (bongo_cat_\w+)_map\[\] = \{([\s\S]*?)\};/g)) {
  const bytes = [...m[2].matchAll(/0x([0-9a-f]{2})/gi)].map(x => parseInt(x[1], 16));
  if (bytes.length !== 190) throw Error(`Unexpected pixel data size: ${m[1]}`);
  sprites[m[1]] = Array.from({length: 26}, (_, y) => Array.from({length: 50}, (_, x) => (bytes[8 + y * 7 + (x >> 3)] >> (7 - x % 8)) & 1));
}
const font = {
 A:['01110','10001','10001','11111','10001','10001','10001'],
 D:['11110','10001','10001','10001','10001','10001','11110'],
 E:['11111','10000','10000','11110','10000','10000','11111'],
 F:['11111','10000','10000','11110','10000','10000','10000'],
 G:['01111','10000','10000','10111','10001','10001','01110'],
 I:['111','010','010','010','010','010','111'],
 L:['10000','10000','10000','10000','10000','10000','11111'],
 M:['10001','11011','10101','10101','10001','10001','10001'],
 N:['10001','11001','10101','10011','10001','10001','10001'],
 O:['01110','10001','10001','10001','10001','10001','01110'],
 S:['01111','10000','10000','01110','00001','00001','11110'],
 T:['11111','00100','00100','00100','00100','00100','00100'],
 U:['10001','10001','10001','10001','10001','10001','01110'],
 W:['10001','10001','10001','10101','10101','10101','01010'],
 '-':['000','000','000','111','000','000','000'],
 ' ':['000','000','000','000','000','000','000']
};
const W = 248, H = 174;
function render(sprite, label) {
  const px = new Uint8Array(W * H); // white background
  const dot = (x,y,c=1) => { if(x>=0&&x<W&&y>=0&&y<H) px[y*W+x]=c; };
  function text(s, y) {
    const width = [...s].reduce((n,c) => n + font[c][0].length + 1, -1) * 2;
    let x = Math.floor((W-width)/2);
    for (const c of s) { const glyph=font[c]; glyph.forEach((r,yy)=>[...r].forEach((v,xx)=>{if(v==='1')for(let dy=0;dy<2;dy++)for(let dx=0;dx<2;dx++)dot(x+xx*2+dx,y+yy*2+dy);})); x+=(glyph[0].length+1)*2; }
  }
  text('DANGGEUN-I', 10);
  // Pixel-perfect 4x nearest-neighbor scaling inside a screen-like frame.
  for(let x=19;x<=228;x++){dot(x,33);dot(x,146);}
  for(let y=33;y<=146;y++){dot(19,y);dot(228,y);}
  sprite.forEach((r,y)=>r.forEach((v,x)=>{if(v)for(let dy=0;dy<4;dy++)for(let dx=0;dx<4;dx++)dot(24+x*4+dx,38+y*4+dy);}));
  text(label, 154);
  return px;
}
// Simple deterministic GIF89a encoder. Reset LZW after each pair of pixels,
// keeping all codes three bits wide and avoiding any external encoder dependency.
const chunks=[];
const push=(...xs)=>chunks.push(Buffer.from(xs));
const word=n=>[n&255,n>>8];
chunks.push(Buffer.from('GIF89a')); push(...word(W),...word(H),0xF0,0,0,255,255,255,0,0,0);
push(0x21,0xFF,11); chunks.push(Buffer.from('NETSCAPE2.0')); push(3,1,0,0,0);
let frameCount=0, totalMs=0;
for(const [band,label,repeats] of [['idle','IDLE',1],['slow','SLOW',2],['mid','MID',4],['fast','FAST',5]]) {
  const seq=animation.match(new RegExp(`const lv_img_dsc_t \\*${band}_imgs\\[\\] = \\{([\\s\\S]*?)\\};`));
  if(!seq)throw Error(`Missing sequence: ${band}`);
  const names=[...seq[1].matchAll(/&(bongo_cat_\w+)/g)].map(m=>m[1]);
  const duration=Number(animation.match(new RegExp(`#define ANIMATION_SPEED_${band.toUpperCase()} (\\d+)`))[1]);
  const delay=Math.max(2,Math.round(duration/names.length/10));
  for(let repeat=0;repeat<repeats;repeat++)for(const name of names){
    if(!sprites[name])throw Error(`Missing sprite: ${name}`);
    const px=render(sprites[name],label);
    push(0x21,0xF9,4,4,...word(delay),0,0);
    push(0x2C,0,0,0,0,...word(W),...word(H),0,2);
    const packed=[];let bits=0,count=0;
    function code(c){bits|=c<<count;count+=3;while(count>=8){packed.push(bits&255);bits>>=8;count-=8;}}
    for(let i=0;i<px.length;i+=2){code(4);code(px[i]);if(i+1<px.length)code(px[i+1]);}
    code(5);if(count)packed.push(bits&255);
    for(let i=0;i<packed.length;i+=255){const block=packed.slice(i,i+255);push(block.length);chunks.push(Buffer.from(block));}push(0);
    frameCount++;totalMs+=delay*10;
  }
}
push(0x3B);
const out=new URL('oled-preview.gif',root);
fs.writeFileSync(out,Buffer.concat(chunks));
console.log(`Generated ${out.pathname}: ${frameCount} frames, ${totalMs}ms, ${W}x${H}, ${fs.statSync(out).size} bytes`);
