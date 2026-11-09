"""Private actor-controller inputs for collision derivation, never exported.

Original animation graphs, state names and masks stay in development memory.
This reader does not select a deliverable collision representation or provide a
runtime dependency. Unknown graph behavior fails instead of inventing a pose.
"""
from dataclasses import dataclass
import math
from xml.etree import ElementTree
import zipfile

from quality_profiles import records, unpack
from hitbox_metadata import additive_values, blend_values, mixer_weights
from authority_data import float32


def controller_resources(archives, names):
    names = set(names)
    if not names or any(not isinstance(n, str) or n != n.casefold()
                        or '/' in n or '\\' in n or not n.endswith(('.atb','.stb')) for n in names):
        raise ValueError('Invalid private controller resource names')
    result = {}
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            if 'levels.gla' not in archive.namelist():
                continue
            source = archive.read('levels.gla')
        mounted = {}
        for name, method, payload, size, crc in records(source,b'levels.gla'):
            key = name.casefold()
            if key not in names:
                continue
            if key in mounted or size > 65536:
                raise ValueError('Ambiguous or oversized private controller')
            mounted[key] = unpack(method,payload,size,crc)
        result.update(mounted)
    if set(result) != names:
        raise ValueError('Missing private controller resource')
    return result


def xml_root(data):
    if len(data) > 65536 or b'<!' in data:
        raise ValueError('Unsupported private controller XML')
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise ValueError('Malformed private controller XML') from error
    if sum(1 for _ in root.iter()) > 256:
        raise ValueError('Oversized private controller tree')
    return root


def attributes(element, required, optional=()):
    if (not set(required) <= element.attrib.keys()
            or element.attrib.keys()-set(required)-set(optional)
            or (element.text or '').strip() or (element.tail or '').strip()):
        raise ValueError('Unsupported private controller fields')
    return element.attrib


def number(value):
    try:
        result = float(value)
    except ValueError as error:
        raise ValueError('Invalid private controller number') from error
    if not math.isfinite(result):
        raise ValueError('Non-finite private controller number')
    return result


def integer(value):
    result = number(value)
    if result != int(result) or not 0 <= result <= 1000000:
        raise ValueError('Invalid private controller integer')
    return int(result)


@dataclass(frozen=True)
class Node:
    name: str
    kind: str
    children: tuple
    lock_start: bool = False


@dataclass(frozen=True)
class ClipClock:
    """Private native slot clock; each tick truncates scaled milliseconds."""
    start: int
    length: int
    speed: float
    flags: int = 0
    current: int = 0
    previous: int = 0

    def __post_init__(self):
        if (any(type(v) is not int or not 0 <= v <= 0x3fffffff
                for v in (self.start,self.length,self.current,self.previous))
                or self.length == 0 or max(self.current,self.previous) >= self.length
                or not math.isfinite(self.speed) or abs(self.speed) > 100
                or type(self.flags) is not int or not 0 <= self.flags <= 0xffffffff):
            raise ValueError('Invalid private animation clock')

    def advance(self, delta_ms):
        if type(delta_ms) is not int or not 0 <= delta_ms <= 1000000:
            raise ValueError('Invalid private animation delta')
        if self.flags & 0x800:
            return self,False,(),()
        current,previous,flags = self.current,self.previous,self.flags
        end = self.start+self.length
        reverse = self.speed < 0
        if flags & 0x200:
            current = 0
        elif flags & 0x440:
            current = self.length//2
        else:
            previous = current
            current += int(float32(float32(delta_ms)*abs(float32(self.speed))))
            flags = flags | 0x2000 if current >= self.length else flags & ~0x2000
            if flags & 0x2000:
                current = current % self.length if flags & 0x1000 else self.length-1
        sample = end-current if reverse else self.start+current
        state = ClipClock(self.start,self.length,self.speed,flags,current,previous)
        if self.flags & 0x640:
            return state,True,(float32(sample),),()
        old_sample = end-previous if reverse else self.start+previous
        if reverse:
            windows = ((self.start,old_sample),(sample,end)) if old_sample < sample else ((sample,old_sample),)
        else:
            windows = ((old_sample,end),(self.start,sample)) if old_sample > sample else ((old_sample,sample),)
        events = tuple(tuple(map(float32,(*window,self.start,end))) for window in windows)
        playing = bool(flags & 0x1000) if not self.flags & 0x2000 and flags & 0x2000 else True
        return state,playing,(float32(sample),),events


def remote_action(event, aimed, actions):
    """Verified state137 subset: idle, aim and reload; others do not select."""
    if type(event) is not int or len(actions) != 7:
        raise ValueError('Invalid remote action selection')
    if event in (0,1):
        return False,actions[0]
    if event == 2:
        return True,actions[1]
    if event == 3:
        return aimed,actions[3 if aimed else 2]
    return aimed,None


def state_restarts(previous_clip, selected_clip, request_flags, finished):
    """State-setting flag2 preserves an unfinished matching clip; flag1 forces."""
    if type(request_flags) is not int or not 0 <= request_flags <= 0xffffffff:
        raise ValueError('Invalid state selection flags')
    return bool(request_flags & 1 or not request_flags & 2 or finished or previous_clip != selected_clip)


class ControllerGraph:
    def __init__(self, graph, settings):
        self.nodes, self.settings = {}, {}

        def visit(element, depth):
            if depth > 32 or element.tag not in ('root','node','animator'):
                raise ValueError('Invalid private controller node')
            leaf = element.tag == 'animator'
            fields = attributes(element,('name',) if leaf else ('name','type'),('lock0',) if leaf else ())
            name = fields['name']
            if not name or len(name) > 128 or name in self.nodes:
                raise ValueError('Duplicate or invalid private controller name')
            kind = 'animator' if leaf else fields['type']
            if kind not in ('animator','blend','mixer','additive','directional','directional_master'):
                raise ValueError('Unsupported private controller node type')
            if (leaf and len(element)) or (not leaf and len(element) != 2):
                raise ValueError('Unsupported private controller arity')
            if fields.get('lock0','false') not in ('false','true'):
                raise ValueError('Invalid private controller lock')
            self.nodes[name] = None
            children = tuple(visit(child,depth+1) for child in element)
            self.nodes[name] = Node(name,kind,children,fields.get('lock0')=='true')
            return name

        root = xml_root(graph)
        if root.tag != 'root':
            raise ValueError('Missing private controller root')
        self.root = visit(root,0)
        source = xml_root(settings)
        if source.tag != 'statetree':
            raise ValueError('Missing private controller settings root')
        attributes(source,())
        for element in source:
            leaf = element.tag == 'animator'
            if element.tag not in ('node','animator'):
                raise ValueError('Unsupported private controller setting')
            fields = attributes(element,('name','state') if leaf else ('name',),() if leaf else ('time',))
            name = fields['name']
            if name not in self.nodes or name in self.settings or leaf != (self.nodes[name].kind=='animator'):
                raise ValueError('Unbound or duplicate private controller setting')
            if leaf:
                if len(element) or not fields['state']:
                    raise ValueError('Invalid private initial state')
                self.settings[name] = {'state':fields['state']}
                continue
            values = {'weights':{},'directions':[],'speeds':[]}
            if 'time' in fields:
                values['duration'] = integer(fields['time'])
            for child in element:
                if len(child):
                    raise ValueError('Nested private controller setting')
                if child.tag == 'weight':
                    field = attributes(child,('index','value'))
                    index = integer(field['index'])
                    if index >= 2 or index in values['weights']:
                        raise ValueError('Invalid private child weight')
                    values['weights'][index] = number(field['value'])
                elif child.tag == 'filter':
                    field = attributes(child,('index','value','isAdditive'))
                    if 'filter' in values or field['isAdditive'] not in ('true','false') or integer(field['value']) not in (0,1):
                        raise ValueError('Invalid private mixer filter')
                    values['filter'] = (integer(field['index']),integer(field['value']),field['isAdditive']=='true')
                elif child.tag == 'default_direction':
                    field = attributes(child,('v',))
                    direction = tuple(number(v) for v in field['v'].split(','))
                    if len(direction) != 3 or 'forward' in values or not any(direction):
                        raise ValueError('Invalid private default direction')
                    values['forward'] = direction
                elif child.tag == 'direction':
                    field = attributes(child,('angle','state'))
                    angle = number(field['angle'])
                    if not 0 <= angle < 360 or not field['state'] or (values['directions'] and angle <= values['directions'][-1][0]):
                        raise ValueError('Invalid private angular state')
                    values['directions'].append((angle,field['state']))
                elif child.tag == 'directional':
                    field = attributes(child,('child','speed'))
                    speed = number(field['speed'])
                    if field['child'] not in self.nodes[name].children or speed <= 0 or (values['speeds'] and speed <= values['speeds'][-1][1]):
                        raise ValueError('Invalid private locomotion child')
                    values['speeds'].append((field['child'],speed))
                else:
                    raise ValueError('Unsupported private controller setting')
            kind = self.nodes[name].kind
            if ((kind == 'directional' and (len(values['directions']) < 2 or 'forward' not in values))
                    or (kind == 'directional_master' and tuple(v[0] for v in values['speeds']) != self.nodes[name].children)
                    or (values['directions'] and kind != 'directional')
                    or (values['speeds'] and kind != 'directional_master')
                    or ('filter' in values and kind != 'mixer')):
                raise ValueError('Inconsistent private controller settings')
            self.settings[name] = values

    def evaluate(self, samples, weights=None, masks=None):
        """Compose already-sampled local TRS poses through the private graph.

        State/clock selection, locked-start sampling and root motion belong to
        the caller and are not inferred here. Scale animation is not supported
        by the verified marine path. Inactive branches need no sample.
        """
        weights, masks = weights or {}, masks or {}
        if (set(samples)-self.nodes.keys() or set(weights)-self.nodes.keys()
                or set(masks)-self.nodes.keys()):
            raise ValueError('Unbound collision pose input')

        def combine(poses, factors, additive):
            if len(poses) == 1:
                return poses[0]
            if not poses or any(len(pose) != len(poses[0]) for pose in poses):
                raise ValueError('Inconsistent collision pose dimensions')
            output = []
            for rows in zip(*poses):
                if any(row[2] != rows[0][2] for row in rows):
                    raise ValueError('Unverified collision scale animation')
                consumer = additive_values if additive else blend_values
                output.append((consumer(1,[row[0] for row in rows],factors),
                               consumer(5,[row[1] for row in rows],factors),rows[0][2]))
            return tuple(output)

        def visit(name):
            node = self.nodes[name]
            if node.kind == 'animator':
                pose = samples.get(name)
                if (not pose or any(len(row) != 3 or tuple(map(len,row)) != (3,4,3)
                        or not all(math.isfinite(v) for component in row for v in component) for row in pose)):
                    raise ValueError('Missing or invalid active collision pose')
                return tuple(pose)
            settings = self.settings.get(name,{})
            factors = weights.get(name,tuple(settings.get('weights',{}).get(i,0.) for i in range(2)))
            if len(factors) != 2 or not all(math.isfinite(w) for w in factors):
                raise ValueError('Invalid collision graph weights')
            active = [i for i,w in enumerate(factors) if w != 0]
            if not active:
                raise ValueError('Collision graph has no active child')
            poses = [visit(node.children[i]) for i in active]
            if len(active) == 1:
                return poses[0]
            if node.kind == 'mixer':
                mask = masks.get(name)
                if mask is None or len(mask) != len(poses[0]) or len(poses[0]) != len(poses[1]):
                    raise ValueError('Missing or inconsistent collision mixer mask')
                additive = settings.get('filter',(0,0,False))[2]
                output = []
                for index,selection in enumerate(mask):
                    row_factors = mixer_weights(selection,factors,additive)
                    selected = [i for i,w in enumerate(row_factors) if w != 0]
                    output.extend(combine([poses[i][index:index+1] for i in selected],
                                          [row_factors[i] for i in selected],additive))
                return tuple(output)
            return combine(poses,[factors[i] for i in active],node.kind=='additive')

        return visit(self.root)
