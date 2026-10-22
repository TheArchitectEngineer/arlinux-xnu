"""Deterministic external input, structured UIKit state and composed pixel checks.
No OCR, image matching model or injected app action.
Production framework adaptation is allowed; the harness supplies no substitute.
"""
import json
import re
import subprocess
import time
from pathlib import Path
from .context import ROOT,command,sha

class Desktop:
    def configure_display(self):
        profile=self.case.get('display')
        if profile:
            values=[profile.get(key) for key in ('width','height','scale')]
            if any(type(value) is not int or value<=0 for value in values):
                raise RuntimeError('Display profile requires positive integer points and scale')
            width,height=values[0]*values[2],values[1]*values[2]
            if width>1600 or height>1600:raise RuntimeError('Display profile exceeds the runner display')
        else:width,height=1024,600
        self.display={'requested':profile,'bufferPixels':[width,height],'reference':self.reference}
        if self.reference:return  # A viewport step must verify the actual Apple window; never claim an applied profile.
        ids=command('xdotool','search','--onlyvisible','--class','[Ww]eston',capture_output=True,text=True).stdout.split()
        if len(ids)!=1:raise RuntimeError('No unique real nested compositor for display configuration')
        command('xdotool','windowsize',ids[0],width,height)
        deadline=time.monotonic()+5
        while True:
            fields=dict(line.split('=',1) for line in command('xdotool','getwindowgeometry','--shell',ids[0],capture_output=True,text=True).stdout.splitlines() if '=' in line)
            if (int(fields['WIDTH']),int(fields['HEIGHT']))==(width,height):
                self.display['observedBufferPixels']=[width,height];return
            if time.monotonic()>=deadline:raise RuntimeError('Real compositor did not accept the display profile')
            time.sleep(.05)

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
            try:
                bounds=json.loads(command(ROOT/'build/reference-tools/desktop-events','bounds',self.processes[self.current]['process'].pid,capture_output=True,text=True).stdout)
            except (subprocess.SubprocessError,ValueError):
                self.infrastructure_failure=True
                raise
            if bounds is None:return None
            return bounds[0],bounds[1],bounds[2]/state['viewport'][0],bounds[2]/state['viewport'][0]
        ids=command('xdotool','search','--onlyvisible','--class','[Ww]eston',capture_output=True,text=True).stdout.split()
        if len(ids)!=1:raise RuntimeError('No unique real nested compositor window')
        geometry=command('xdotool','getwindowgeometry','--shell',ids[0],capture_output=True,text=True).stdout
        fields=dict(line.split('=',1) for line in geometry.splitlines() if '=' in line)
        stderr=Path(self.processes[self.current]['stdout']).with_suffix('.stderr').read_text(errors='replace')
        sizes=re.findall(r'WAYLAND_BACKEND_READY width=(\d+) height=(\d+)',stderr)
        if not sizes:raise RuntimeError('No actual configured fullscreen framebuffer geometry')
        width,height=map(int,sizes[-1])
        logical_width,logical_height=state['viewport']
        if logical_width<=0 or logical_height<=0 or width<=0 or height<=0:
            raise RuntimeError('Invalid logical viewport or configured framebuffer')
        # Inspection rectangles are UIKit points. The actual desktop window
        # contains buffer pixels, so include the point-to-buffer conversion.
        return int(fields['X']),int(fields['Y']),int(fields['WIDTH'])/logical_width,int(fields['HEIGHT'])/logical_height

    def screen_rect(self,state,rect):
        window=self.window(state)
        if window is None:
            self.steps[-1]['windowPendingPolls']=self.steps[-1].get('windowPendingPolls',0)+1
            return None
        x,y,sx,sy=window
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
        occupied=[i for i,count in enumerate(crop.convert('L').histogram()) if count>=8]
        return {'pixels':len(pixels),'ink':sum(min(p)<220 for p in pixels),'white':sum(min(p)>240 for p in pixels),
                'red':sum(p[0]>200 and p[1]<70 and p[2]<70 for p in pixels),
                'green':sum(p[1]>200 and p[0]<70 and p[2]<70 for p in pixels),
                'contrastSpan':occupied[-1]-occupied[0] if occupied else 0,
                'sha256':hashlib.sha256(crop.tobytes()).hexdigest()}

    def observe(self,pattern,word=False,timeout_seconds=8,region=None):
        if type(timeout_seconds) not in (int,float) or not 0<timeout_seconds<=120:
            raise RuntimeError('UI observation timeout must be positive and at most 120 seconds')
        if region is not None and (not isinstance(region,list) or len(region)!=4 or
                any(type(value) not in (int,float) for value in region) or
                not (0<=region[0]<1 and 0<=region[1]<1 and region[2]>0 and region[3]>0 and
                     region[0]+region[2]<=1 and region[1]+region[3]<=1)):
            raise RuntimeError('Observation region must be inside the normalized viewport')
        deadline=time.monotonic()+timeout_seconds;regex=re.compile(pattern,re.I)
        while True:
            self.guard();state=self.inspection()
            matches=[] if not state else [item for item in state['elements'] if regex.search(item['text']) and (not word or item['interactive'])]
            if region is not None and state:
                width,height=state['viewport']
                left,top,right,bottom=region[0]*width,region[1]*height,(region[0]+region[2])*width,(region[1]+region[3])*height
                matches=[item for item in matches if left<=item['rect'][0] and top<=item['rect'][1] and
                         item['rect'][0]+item['rect'][2]<=right and item['rect'][1]+item['rect'][3]<=bottom]
            if len(matches)>1:raise RuntimeError('Ambiguous structured UI target: '+pattern)
            if matches:
                rect=self.screen_rect(state,matches[0]['rect'])
                if rect is None:
                    if time.monotonic()>=deadline:raise RuntimeError('No visible Apple-host window before the UI observation deadline: '+pattern)
                    time.sleep(.2);continue
                path=self.snapshot('observe');pixels=self.sample(path,rect)
                self.observations[-1].update(uiState=state,target=matches[0],screenRect=rect,pixels=pixels)
                # Require ink on a light surface, including native gray controls.
                # The complement of ink uses the same threshold on both hosts;
                # pure black/white/gray frames cannot meet both requirements.
                # Structured state or a frame callback alone cannot pass.
                key=(self.current,self.processes[self.current]['generation'],tuple(matches[0]['rect']))
                old=self.painted.get(key)
                changed=not old or old['text']==matches[0]['text'] or old['sha256']!=pixels['sha256']
                painted=pixels['ink']>=8 and pixels['pixels']-pixels['ink']>=pixels['pixels']//2
                if self.apps[self.current].get('pixelAppearance')=='mixed':
                    # Original apps may have dark, colored or light surfaces.
                    # Require contrasting real pixels, not a particular theme.
                    painted=pixels['contrastSpan']>=48
                if changed and painted:
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
                rect=self.screen_rect(state,[0,0,*state['viewport']])
                if rect is None:
                    if time.monotonic()>=deadline:raise RuntimeError('No visible Apple-host window before the pixel observation deadline')
                    time.sleep(.2);continue
                path=self.snapshot('pixels');pixels=self.sample(path,rect)
                self.observations[-1].update(screenRect=rect,pixels=pixels)
                if pixels[step['color']]>=step['minimum']:return
            if time.monotonic()>=deadline:raise RuntimeError('Missing composed color effect: '+step['color'])
            time.sleep(.2)
