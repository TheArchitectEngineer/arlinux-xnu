import csv
import io
import re
import time
from .context import ROOT,command,sha

class Desktop:
    def snapshot(self, label):
        path = self.directory / f'{len(self.observations):03d}-{label}.png'
        command('screencapture','-x',path) if self.reference else command('scrot',path)
        result = command('tesseract', path, 'stdout', '--psm', '11', 'tsv',
                         capture_output=True, text=True)
        path.with_suffix('.tsv').write_text(result.stdout)
        words = [row for row in csv.DictReader(io.StringIO(result.stdout), delimiter='\t')
                 if row['text'].strip()]
        text = ' '.join(word['text'] for word in words)
        path.with_suffix('.txt').write_text(text + '\n')
        self.observations.append({'screenshot': str(path.relative_to(self.output)),
                                  'sha256': sha(path), 'text': text,
                                  'timeMonotonic': time.monotonic()})
        return words, text

    def observe(self, pattern, word=False):
        deadline = time.monotonic() + 8
        regex = re.compile(pattern, re.I)
        while True:
            self.guard()
            words, text = self.snapshot('observe')
            if word:
                matches = [item for item in words if regex.search(item['text'])]
                if len(matches) == 1:
                    return matches[0]
                if len(matches) > 1:
                    raise RuntimeError('Ambiguous visible control: ' + pattern)
            elif regex.search(text):
                return
            if time.monotonic() >= deadline:
                raise RuntimeError('Required compositor-visible effect missing: ' + pattern)
            time.sleep(.5)

    def click_word(self, target):
        x = int(target['left']) + int(target['width'])//2
        y = int(target['top']) + int(target['height'])//2
        if self.reference:
            command(ROOT/'build/reference-tools/desktop-events','click',x,y)
            return
        command('xdotool', 'mousemove', '--sync', x, y)
        command('xdotool', 'mousedown', '1')
        time.sleep(.08)
        command('xdotool', 'mouseup', '1')

    def observe_pixels(self,step):
        deadline=time.monotonic()+8
        while True:
            self.guard();self.snapshot('pixels')
            image=self.directory / self.observations[-1]['screenshot'].split('/')[-1]
            if self.reference:
                count=int(command(ROOT/'build/reference-tools/desktop-events','pixels',image,step['color'],capture_output=True,text=True).stdout)
            else:
                from PIL import Image
                colors=Image.open(image).convert('RGB').getdata()
                channel={'red':0,'green':1}[step['color']]
                count=sum(pixel[channel]>200 and all(pixel[i]<70 for i in range(3) if i!=channel) for pixel in colors)
            self.observations[-1]['colorEffect']={'color':step['color'],'pixels':count,'minimum':step['minimum']}
            if count>=step['minimum']:return
            if time.monotonic()>=deadline:raise RuntimeError('Missing composed color effect: '+step['color'])
            time.sleep(.5)
