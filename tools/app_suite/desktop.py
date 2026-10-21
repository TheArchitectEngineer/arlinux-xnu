"""Deterministic external input, structured UIKit state and composed pixel checks.
No OCR, image matching model or injected app action.
Production framework adaptation is allowed; the harness supplies no substitute.
"""
import json
import re
import time
from pathlib import Path
from .context import ROOT,command,sha

class Desktop:
    def snapshot(self,label):
        path=self.directory/f'{len(self.observations):03d}-{label}.png'
        command('screencapture','-x',path) if self.reference else command('scrot',path)
        observation={'screenshot':str(path.relative_to(self.output)),'sha256':sha(path),'timeMonotonic':time.monotonic()}
        self.observations.append(observation)
        return path

    def inspection(self,name=None):
        item=self.processes[name or self.current]
        events=[]
        for line in Path(item['stdout']).read_text(errors='replace').splitlines():
            if line.startswith('APP_INSPECTION '):
                try:events.append(json.loads(line[15:]))
                except ValueError:pass  # original writer has not completed its line
        if not events:return None
        self.observations_state=events[-1]
        return events[-1]

    def window(self,state):
        if self.reference:
            bounds=json.loads(command(ROOT/'build/reference-tools/desktop-events','bounds',self.processes[self.current]['process'].pid,capture_output=True,text=True).stdout)
            return bounds[0],bounds[1],bounds[2]/state['viewport'][0],bounds[2]/state['viewport'][0]
        ids=command('xdotool','search','--onlyvisible','--class','[Ww]eston',capture_output=True,text=True).stdout.split()
        if len(ids)!=1:raise RuntimeError('No unique real nested compositor window')
        geometry=command('xdotool','getwindowgeometry','--shell',ids[0],capture_output=True,text=True).stdout
        fields=dict(line.split('=',1) for line in geometry.splitlines() if '=' in line)
        stderr=Path(self.processes[self.current]['stdout']).with_suffix('.stderr').read_text(errors='replace')
        sizes=re.findall(r'WAYLAND_BACKEND_READY width=(\d+) height=(\d+)',stderr)
        if not sizes:raise RuntimeError('No actual configured fullscreen framebuffer geometry')
        width,height=map(int,sizes[-1])
        return int(fields['X']),int(fields['Y']),int(fields['WIDTH'])/width,int(fields['HEIGHT'])/height

    def screen_rect(self,state,rect):
        x,y,sx,sy=self.window(state)
        return [round(x+rect[0]*sx),round(y+rect[1]*sy),max(1,round(rect[2]*sx)),max(1,round(rect[3]*sy))]

    def sample(self,path,rect):
        if self.reference:
            return json.loads(command(ROOT/'build/reference-tools/desktop-events','sample',path,*rect,capture_output=True,text=True).stdout)
        from PIL import Image
        import hashlib
        x,y,w,h=rect
        with Image.open(path) as image:
            if x<0 or y<0 or x+w>image.width or y+h>image.height:raise RuntimeError('Target is outside actual composed display')
            crop=image.convert('RGB').crop((x,y,x+w,y+h));pixels=list(crop.getdata())
        return {'pixels':len(pixels),'ink':sum(min(p)<220 for p in pixels),'white':sum(min(p)>240 for p in pixels),
                'red':sum(p[0]>200 and p[1]<70 and p[2]<70 for p in pixels),
                'green':sum(p[1]>200 and p[0]<70 and p[2]<70 for p in pixels),
                'sha256':hashlib.sha256(crop.tobytes()).hexdigest()}

    def observe(self,pattern,word=False,timeout_seconds=8):
        if type(timeout_seconds) not in (int,float) or not 0<timeout_seconds<=120:
            raise RuntimeError('UI observation timeout must be positive and at most 120 seconds')
        deadline=time.monotonic()+timeout_seconds;regex=re.compile(pattern,re.I)
        while True:
            self.guard();state=self.inspection()
            matches=[] if not state else [item for item in state['elements'] if regex.search(item['text']) and (not word or item['interactive'])]
            if len(matches)>1:raise RuntimeError('Ambiguous structured UI target: '+pattern)
            if matches:
                path=self.snapshot('observe');rect=self.screen_rect(state,matches[0]['rect']);pixels=self.sample(path,rect)
                self.observations[-1].update(uiState=state,target=matches[0],screenRect=rect,pixels=pixels)
                # Actual text/control must have ink against the fixture's white UI.
                # Readiness, exported UIKit state or a frame callback alone cannot pass.
                key=(self.current,self.processes[self.current]['generation'],tuple(matches[0]['rect']))
                old=self.painted.get(key)
                changed=not old or old['text']==matches[0]['text'] or old['sha256']!=pixels['sha256']
                if changed and pixels['ink']>=8 and pixels['white']>=pixels['pixels']//2:
                    self.painted[key]={'text':matches[0]['text'],'sha256':pixels['sha256']}
                    return {**matches[0],'screenRect':rect}
            if time.monotonic()>=deadline:raise RuntimeError('Missing structured UI state or actual composited pixels: '+pattern)
            time.sleep(.2)

    def click_target(self,target):
        x,y,w,h=target['screenRect'];x+=w//2;y+=h//2
        if self.reference:command(ROOT/'build/reference-tools/desktop-events','click',x,y)
        else:
            # --sync waits for movement, so a second click at the same actual
            # coordinates can hang. Confirm the real pointer position instead.
            command('xdotool','mousemove',x,y)
            deadline=time.monotonic()+2
            while True:
                self.guard()
                position=command('xdotool','getmouselocation','--shell',capture_output=True,text=True).stdout
                fields=dict(line.split('=',1) for line in position.splitlines() if '=' in line)
                if int(fields['X'])==x and int(fields['Y'])==y:break
                if time.monotonic()>=deadline:raise RuntimeError('External pointer did not reach the actual target')
                time.sleep(.02)
            command('xdotool','mousedown','1')
            time.sleep(.08);command('xdotool','mouseup','1')

    def observe_pixels(self,step):
        deadline=time.monotonic()+8
        while True:
            self.guard();state=self.inspection()
            if state:
                path=self.snapshot('pixels');rect=self.screen_rect(state,[0,0,*state['viewport']]);pixels=self.sample(path,rect)
                self.observations[-1].update(screenRect=rect,pixels=pixels)
                if pixels[step['color']]>=step['minimum']:return
            if time.monotonic()>=deadline:raise RuntimeError('Missing composed color effect: '+step['color'])
            time.sleep(.2)
