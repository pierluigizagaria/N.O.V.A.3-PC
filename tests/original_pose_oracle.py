"""Opt-in original pose-controller oracle; no original resources are emitted.

Execute bounded functions/blocks from the pinned original library with numeric
fixtures. Scene/physics access, presentation effects and external math boundaries
are intercepted as documented per probe. Animation selection, clock arithmetic,
quaternion products and update ordering execute in the emulator. This is not a
game runtime. Optional dependencies are in original_pose_requirements.txt.
"""
import hashlib
import io
import math
import struct


class OriginalPoseOracle:
    def __init__(self, library):
        from elftools.elf.elffile import ELFFile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as registers
        source = library.read_bytes()
        if hashlib.sha256(source).hexdigest() != 'a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e':
            raise ValueError('Unverified original pose-controller input')
        self.r = registers
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0, 0x1200000)
        for segment in ELFFile(io.BytesIO(source)).iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                self.cpu.mem_write(segment['p_vaddr'], segment.data())
        self.cpu.mem_map(0x2000000, 0x10000)
        self.cpu.mem_map(0x3000000, 0x10000)
        self.word(0x300e000+0x94, 0x300f000)
        self.word(0x300e000+0x98, 0x300f010)
        self.cpu.mem_write(0x300f000, b'\xc3')
        self.cpu.mem_write(0x300f010, b'\xc3')
        self.cpu.hook_add(UC_HOOK_CODE, self.scene_boundary, begin=0x300f000, end=0x300f010)
        # The ELF's libm wrapper depends on dynamic relocation. Supply only the
        # standard acosf boundary; direction selection/weights remain original.
        self.cpu.mem_write(0x300f020, b'\xd9\x05'+struct.pack('<I', 0x300f030)+b'\xc3')
        for address in (0xdb4390,0xdb3de0,0xdb4240):
            self.cpu.hook_add(UC_HOOK_CODE, self.math_boundary, begin=address, end=address)
        self.cpu.mem_write(0x300f040, b'\xc3')
        for address in (0x26cc70, 0x26cc50, 0x5d29a0):
            self.cpu.hook_add(UC_HOOK_CODE, self.speed_boundary, begin=address, end=address)
        self.cpu.mem_write(0x300f050, b'\xc3')
        for address in (0x300f050,0xcf250,0xcf0e0):
            self.cpu.hook_add(UC_HOOK_CODE, self.body_boundary, begin=address, end=address)
        for address in (0x300f060,0x300f070):
            self.cpu.mem_write(address,b'\xc3')
            self.cpu.hook_add(UC_HOOK_CODE,self.clock_boundary,begin=address,end=address)
        for address in (0x57d660,0x6271d0,0x5d5800,0x5d22b0):
            self.cpu.hook_add(UC_HOOK_CODE,self.remote_boundary,begin=address,end=address)
        for address in (0x26cdd0,0x26ce30,0x26cd90,0x271820,0x279870,0xc02d4):
            self.cpu.hook_add(UC_HOOK_CODE,self.state_boundary,begin=address,end=address)

    def word(self, address, value=None):
        if value is None:
            return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]
        self.cpu.mem_write(address, struct.pack('<I', value))

    def scene_boundary(self, cpu, address, size, _):
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        node = self.word(stack+4)
        if not 0x3000000 <= node < 0x3000600 or (node-0x3000000) % 0x100:
            raise ValueError('Unexpected original pose node')
        if address == 0x300f000:
            cpu.reg_write(self.r.UC_X86_REG_EAX, node+0x20)
        elif address == 0x300f010:
            cpu.mem_write(node+0x20, bytes(cpu.mem_read(self.word(stack+8), 16)))
            self.writes += 1

    def math_boundary(self, cpu, address, size, _):
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        value = struct.unpack('<f', cpu.mem_read(stack+4, 4))[0]
        function = {0xdb4390:math.acos,0xdb3de0:math.sin,0xdb4240:math.cos}[address]
        cpu.mem_write(0x300f030, struct.pack('<f', function(value)))
        cpu.reg_write(self.r.UC_X86_REG_EIP, 0x300f020)

    def speed_boundary(self, cpu, address, size, _):
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        slot = self.word(stack+8)
        if getattr(self,'capture_state',False) and address == 0x26cc70:
            cpu.reg_write(self.r.UC_X86_REG_EAX,self.state_time)
            cpu.reg_write(self.r.UC_X86_REG_EIP,0x300f040)
            return
        if slot >= 2:
            raise ValueError('Unexpected original speed slot')
        if address == 0x5d29a0:
            self.speed_calls.append((slot, *struct.unpack('<2f', cpu.mem_read(stack+12, 8))))
        else:
            cpu.reg_write(self.r.UC_X86_REG_EAX, self.times[slot] if address == 0x26cc70 else self.lengths[slot])
        cpu.reg_write(self.r.UC_X86_REG_EIP, 0x300f040)

    def body_boundary(self, cpu, address, size, _):
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        if address == 0x300f050:
            cpu.reg_write(self.r.UC_X86_REG_EAX, 0x3008000)
        else:
            dimensions = 3 if address == 0xcf250 else 4
            values = struct.unpack('<'+str(dimensions)+'f', cpu.mem_read(self.word(stack+8),dimensions*4))
            self.body_values.append(values)
            cpu.reg_write(self.r.UC_X86_REG_EIP, 0x300f040)

    def clock_boundary(self, cpu, address, size, _):
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        if address == 0x300f060:
            self.clock_samples.append(struct.unpack('<f',cpu.mem_read(stack+8,4))[0])
        else:
            self.clock_events.append(struct.unpack('<4f',cpu.mem_read(stack+8,16)))

    def remote_boundary(self,cpu,address,size,_):
        if not getattr(self,'capture_remote',False):
            return
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        if address in (0x5d5800,0x5d22b0):
            self.remote_calls.append((address,*struct.unpack('<4i',cpu.mem_read(stack+8,16))))
        elif address == 0x57d660 and self.word(stack+8) != 145:
            raise ValueError('Unexpected remote linked-object lookup')
        cpu.reg_write(self.r.UC_X86_REG_EAX,0)
        cpu.reg_write(self.r.UC_X86_REG_EIP,0x300f040)

    def state_boundary(self,cpu,address,size,_):
        if not getattr(self,'capture_state',False):
            return
        stack = cpu.reg_read(self.r.UC_X86_REG_ESP)
        result = 0
        if address == 0xc02d4:
            a,b,size = struct.unpack('<3I',cpu.mem_read(stack+4,12))
            if size > 128:
                raise ValueError('Unexpected original clip comparison')
            result = int(cpu.mem_read(a,size) != cpu.mem_read(b,size))
        elif address == 0x271820:
            result = self.state_clip_ids[self.word(stack+8)]
        elif address == 0x26cd90:
            result = int(self.state_finished)
        elif address == 0x279870:
            self.state_calls.append(('play',*struct.unpack('<3I',cpu.mem_read(stack+8,12))))
            self.state_time = 0
        elif address == 0x26cdd0:
            self.state_calls.append(('speed',self.word(stack+8),struct.unpack('<f',cpu.mem_read(stack+12,4))[0]))
        elif address == 0x26ce30:
            self.state_calls.append(('loop',self.word(stack+8),bool(self.word(stack+12))))
        cpu.reg_write(self.r.UC_X86_REG_EAX,result)
        cpu.reg_write(self.r.UC_X86_REG_EIP,0x300f040)

    def adjust(self, rotations, pitch_degrees):
        if len(rotations) != 6:
            raise ValueError('Expected root, spine, spine1, neck and both clavicles')
        self.cpu.mem_write(0x2000000, bytes(0x10000))
        frame = 0x200fff8
        for i, (offset, rotation) in enumerate(zip((0x28,0x30,0x38,0x40,0xd0,0x58), rotations)):
            node = 0x3000000 + i*0x100
            self.word(node, 0x300e000)
            self.cpu.mem_write(node+0x20, struct.pack('<4f', *rotation))
            self.word(frame-offset, node)
        pitch = pitch_degrees
        if pitch > 180:
            pitch -= 360
        if pitch < -180:
            pitch += 360
        for offset, coefficient in ((0x104, .006981316953897476), (0x114, .003490658476948738),
                                    (0x124, .003490658476948738), (0x134, .003490658476948738)):
            angle = struct.unpack('<f', struct.pack('<f', pitch*coefficient))[0]
            self.cpu.mem_write(frame-offset, struct.pack('<4f', math.sin(angle*.5), 0, 0, math.cos(angle*.5)))
        for offset, value in ((0x3c4,0x114), (0x3cc,0x124), (0x3c0,0x134)):
            self.word(frame-offset, frame-value)
        for register, value in ((self.r.UC_X86_REG_EBP,frame), (self.r.UC_X86_REG_ESP,0x200fb80),
                                (self.r.UC_X86_REG_EBX,0x113e340), (self.r.UC_X86_REG_EDI,frame-0x104)):
            self.cpu.reg_write(register, value)
        self.writes = 0
        self.cpu.emu_start(0x62db83, 0x62e208, count=10000)
        if self.writes != 5 or self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x62e208:
            raise ValueError('Original aim adjustment did not reach its expected boundary')
        return tuple(struct.unpack('<4f', self.cpu.mem_read(0x3000020+i*0x100, 16)) for i in range(6))

    def direction(self, vector):
        """Original directional weights before its state/time synchronization."""
        self.cpu.mem_write(0x2000000, bytes(0x10000))
        self.cpu.mem_write(0x3001000, bytes(0x8000))
        self.word(0x3001000+0x164, 0x3002000)
        self.word(0x3001000+0x120, 0x3003000)
        self.word(0x3003000+0x24, 0x3004000)
        self.word(0x3004000, 0x3005000)
        self.word(0x3005000+0x28, 0x3006000)
        self.cpu.mem_write(0x3002000+0xc, struct.pack('<6f', 0,1,0,1,0,0))
        self.word(0x3002000+0x24, 0x3007000)
        self.word(0x3002000+0x28, 0x3007020)
        for index in range(4):
            self.cpu.mem_write(0x3007000+index*8, struct.pack('<If', 100+index, index*90))
        self.cpu.mem_write(0x3008000, struct.pack('<3f', *vector))
        stack = 0x200fff0
        self.cpu.mem_write(stack, struct.pack('<4I', 0x300f100,0x3001000,0,0x3008000))
        self.cpu.reg_write(self.r.UC_X86_REG_ESP, stack)
        self.cpu.emu_start(0x5da120, 0x5da334, count=10000)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x5da334:
            raise ValueError('Original directional blend did not reach its expected boundary')
        frame = self.cpu.reg_read(self.r.UC_X86_REG_EBP)
        first, second = (self.word(frame-offset)//8 for offset in (0x1c,0x24))
        return first, second, struct.unpack('<2f', self.cpu.mem_read(0x3006000, 8))

    def speed(self, previous, current, lengths=(1200,800), times=(300,600), thresholds=(.8,1.2)):
        """Original walk/run weight, playback-rate and phase synchronization."""
        self.cpu.mem_write(0x2000000, bytes(0x10000))
        self.cpu.mem_write(0x3001000, bytes(0xa000))
        self.word(0x3001000+0x164, 0x3002000)
        self.word(0x3001000+0x120, 0x3003000)
        self.word(0x3001000+0x17c, 0x3009000)
        self.word(0x3003000+0x24, 0x3004000)
        self.word(0x3004000, 0x3005000)
        self.word(0x3005000+0x28, 0x3006000)
        self.cpu.mem_write(0x3009000, struct.pack('<If3I', 0,previous,0x3009100,0x3009118,0x3009118))
        for i, threshold in enumerate(thresholds):
            self.cpu.mem_write(0x3009100+i*12, struct.pack('<IfI', i,threshold,lengths[i]))
            self.word(0x3002000+i*48+4, i)
        self.lengths, self.times, self.speed_calls = lengths, times, []
        stack = 0x200fff0
        self.cpu.mem_write(stack, struct.pack('<3If', 0x300f100,0x3001000,0,current))
        self.cpu.reg_write(self.r.UC_X86_REG_ESP, stack)
        self.cpu.emu_start(0x5d2a80, 0x300f100, count=10000)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
            raise ValueError('Original speed blend did not return')
        return struct.unpack('<2f', self.cpu.mem_read(0x3006000, 8)), tuple(self.speed_calls)

    def transition(self, duration, weights, updates):
        """Execute original direction setter and integer transition updates."""
        self.cpu.mem_write(0x3001000, bytes(0xa000))
        self.word(0x3001004, 0x300a000)
        self.word(0x3001000+0x120, 0x3003000)
        self.word(0x3003000+0x24, 0x3004000)
        self.word(0x3004000, 0x3005000)
        self.word(0x3005000+0x28, 0x3006000)
        self.cpu.mem_write(0x3006000, struct.pack('<2f', *weights))
        self.word(0x3005000+0x4c, sum(w != 0 for w in weights))
        self.word(0x3001000+0x1a0, 0x3009000)
        self.word(0x3001000+0x1a4, 0x300901c)
        self.cpu.mem_write(0x3009000, struct.pack('<7i', 0,0,1,duration,duration,0,0))
        results = []
        for direction, override, delta in updates:
            for address, arguments in ((0x5d22b0, (0x3001000,0,direction,override,0)),
                                       (0x5d0f60, (0x3001000,delta))):
                self.cpu.mem_write(0x2000000, bytes(0x10000))
                stack = 0x200ff00
                self.cpu.mem_write(stack, struct.pack('<I'+'i'*len(arguments), 0x300f100, *arguments))
                self.cpu.reg_write(self.r.UC_X86_REG_ESP, stack)
                self.cpu.emu_start(address, 0x300f100, count=10000)
                if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
                    raise ValueError('Original transition did not return')
            record = struct.unpack('<7i', self.cpu.mem_read(0x3009000, 28))
            results.append((record[3], record[5], record[6],
                            struct.unpack('<2f', self.cpu.mem_read(0x3006000, 8))))
        return results

    def body_transform(self, matrix, part_id, radius, extension, cylinder):
        """Original live ragdoll placement from one numeric bone matrix.

        Only scene-matrix retrieval and physics position/rotation setters are
        intercepted. Original matrix-to-quaternion and placement math execute.
        Cylinder is the original unextended initial bone-pair cylinder length.
        """
        self.cpu.mem_write(0x2000000, bytes(0x10000))
        self.cpu.mem_write(0x3001000, bytes(0xb000))
        self.word(0x3001000+0x14, 0x3002000)
        self.word(0x3001000+0x10c, 0x3007000)
        self.word(0x3002000+0x2c, 1)
        self.word(0x3002000+0x34, 0x3003000)
        self.word(0x3003000, 0x3004000)
        self.word(0x3002000+0x8c, 0x3005000)
        self.word(0x3005004, 0x3006000)
        self.word(0x3006000, part_id)
        self.cpu.mem_write(0x3006014, struct.pack('<f', radius))
        self.cpu.mem_write(0x3006028, struct.pack('<f', extension))
        self.word(0x3007000, 0x3007100)
        self.word(0x3007100, 0x3007200)
        self.word(0x3007248, 0x300f050)
        self.cpu.mem_write(0x3008000, struct.pack('<16f', *matrix))
        self.word(0x115c120, 0x300b000)
        self.cpu.mem_write(0x300b000, struct.pack('<f', cylinder))
        frame = 0x200fff8
        self.word(frame-0x214, 1)
        angle = struct.unpack('<f',struct.pack('<f',math.pi/2))[0]
        self.cpu.mem_write(frame-0xb8, struct.pack('<4f',0,0,math.sin(angle*.5),math.cos(angle*.5)))
        for register, value in ((self.r.UC_X86_REG_EBP,frame), (self.r.UC_X86_REG_ESP,0x200fa00),
                                (self.r.UC_X86_REG_EBX,0x113e340), (self.r.UC_X86_REG_ESI,0x3001000)):
            self.cpu.reg_write(register,value)
        self.body_values = []
        self.cpu.emu_start(0x7008ee,0x700bfb,count=10000)
        if len(self.body_values) != 2 or self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x700bfb:
            raise ValueError('Original ragdoll placement did not reach its boundary')
        return tuple(self.body_values)

    def track_mask(self, targets, selected, selector):
        """Original track-weight/filter writers with numeric target identities."""
        if not 0 < len(targets) <= 64:
            raise ValueError('Expected a bounded synthetic target list')
        self.cpu.mem_write(0x3001000, bytes(0xa000))
        self.word(0x3001008,0x3002000)
        self.word(0x300100c,0x3002000+4*len(targets))
        self.word(0x3001014,0x3003000)
        self.word(0x3003008,0x3004008)
        self.word(0x3004034,0x3005000)
        self.word(0x3005000,0x3007000)
        self.word(0x3005004,0x3007000+len(targets))
        self.word(0x300403c,0x3006000)
        self.cpu.mem_write(0x3006000,struct.pack('<'+'I'*len(targets),*targets))
        for index in range(3):
            self.word(0x3001018+index*4,0x3008000+index*0x100)
            self.word(0x3008008+index*0x100,0x3009000+index*0x100)
        commands = [(0x8326c0,struct.pack('<If',0x3001000,float(selector==0)))]
        commands.extend((0x8017e0,struct.pack('<IIf',0x3001000,node,float(selector!=0))) for node in selected)
        for address, arguments in commands:
            self.cpu.mem_write(0x2000000,bytes(0x10000))
            self.cpu.mem_write(0x200ff00,struct.pack('<I',0x300f100)+arguments)
            self.cpu.reg_write(self.r.UC_X86_REG_ESP,0x200ff00)
            self.cpu.emu_start(address,0x300f100,count=10000)
            if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
                raise ValueError('Original track mask did not return')
        values = struct.unpack('<'+'f'*len(targets),self.cpu.mem_read(0x3002000,len(targets)*4))
        filters = tuple(struct.unpack('<Q',self.cpu.mem_read(0x3009000+index*0x100,8))[0] for index in range(3))
        return values,filters

    def mixer_weights(self, mask, weights, additive=False):
        """Original track-blender weight calculation before value application."""
        self.cpu.mem_write(0x2000000,bytes(0x10000))
        self.cpu.mem_write(0x3001000,bytes(0x4000))
        self.word(0x3001000+0x54,0x3002000)
        self.word(0x3002008,0x3003000)
        self.word(0x3001000+0x28,0x3004000)
        self.word(0x3001000+0x50,int(additive))
        self.cpu.mem_write(0x3003000,struct.pack('<f',mask))
        self.cpu.mem_write(0x3004000,struct.pack('<2f',*weights))
        frame = 0x200ff00
        self.word(frame+8,0x3001000)
        for register,value in ((self.r.UC_X86_REG_EBP,frame),(self.r.UC_X86_REG_EAX,0),
                               (self.r.UC_X86_REG_EBX,0x113e340)):
            self.cpu.reg_write(register,value)
        self.cpu.emu_start(0x8427e5,0x842830,count=1000)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x842830:
            raise ValueError('Original mixer weights did not reach their boundary')
        return struct.unpack('<2f',self.cpu.mem_read(frame-0x28,8))

    def added_value(self, kind, values, weights):
        """Original additive position/quaternion track consumer."""
        dimensions = {1:3,5:4}[kind]
        if len(values) != len(weights) or len(values) > 64:
            raise ValueError('Invalid additive oracle inputs')
        self.cpu.mem_write(0x2000000,bytes(0x10000))
        self.cpu.mem_write(0x3001000,bytes(0x3000))
        self.cpu.mem_write(0x3001000,struct.pack('<'+'f'*(len(values)*dimensions),*(v for row in values for v in row)))
        self.cpu.mem_write(0x3002000,struct.pack('<'+'f'*len(weights),*weights))
        arguments = (0x300f100,0,0x3001000,0x3002000,len(values),0x3003000)
        self.cpu.mem_write(0x200ff00,struct.pack('<6I',*arguments))
        self.cpu.reg_write(self.r.UC_X86_REG_ESP,0x200ff00)
        self.cpu.emu_start(0x916b30 if kind==1 else 0x909440,0x300f100,count=100000)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
            raise ValueError('Original additive track did not return')
        return struct.unpack('<'+'f'*dimensions,self.cpu.mem_read(0x3003000,dimensions*4))

    def clock(self, start, length, speed, flags, initial, deltas):
        """Original animation-controller clock and event windows, no scripts."""
        self.cpu.mem_write(0x3001000,bytes(0x6000))
        self.word(0x3001020,0x3002000)
        for offset,value in ((0,0),(4,length),(8,start),(12,start+length),(0x1c,initial),
                             (0x28,0x3003000),(0x2c,0x3004000),(0x34,flags)):
            self.word(0x3002000+offset,value)
        self.cpu.mem_write(0x3002018,struct.pack('<f',speed))
        self.word(0x3003000,0x3005000)
        self.word(0x3005010,0x300f060)
        self.word(0x3004000,0x3006000)
        self.word(0x3006010,0x300f070)
        result = []
        for delta in deltas:
            self.cpu.mem_write(0x2000000,bytes(0x10000))
            self.cpu.mem_write(0x200ff00,struct.pack('<4I',0x300f100,0x3001000,0,delta))
            self.cpu.reg_write(self.r.UC_X86_REG_ESP,0x200ff00)
            self.clock_samples,self.clock_events = [],[]
            self.cpu.emu_start(0x2736c0,0x300f100,count=10000)
            if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
                raise ValueError('Original animation clock did not return')
            result.append((self.word(0x300201c),self.word(0x3002020),self.word(0x3002034),
                           bool(self.cpu.reg_read(self.r.UC_X86_REG_EAX)),
                           tuple(self.clock_samples),tuple(self.clock_events)))
        return result

    def remote_states(self,events,actions,aimed=False):
        """Original on-foot state event to action/transition selection.

        Presentation-only linked objects and sounds are absent. State playback
        and transition setters are captured; their own consumers have separate
        clock/transition probes. This does not emulate firing, jumping or seats.
        """
        if len(actions) != 7:
            raise ValueError('Expected seven remote weapon action IDs')
        self.cpu.mem_write(0x3001000,bytes(0x6000))
        for offset,value in ((0x20,0x3002000),(0xa4,0x3003000),(0xa8,0x3003074),
                             (0x130,5),(0x134,0),(0x138,1),(0x13c,0),(0x1b4,0x3006000)):
            self.word(0x3001000+offset,value)
        self.cpu.mem_write(0x3001099,bytes([int(aimed)]))
        self.word(0x300204c,0x3004000)
        self.word(0x3003064,0xffffffff)
        self.cpu.mem_write(0x300303c,struct.pack('<7I',*actions))
        result = []
        self.capture_remote = True
        try:
            for event in events:
                self.cpu.mem_write(0x2000000,bytes(0x10000))
                self.cpu.mem_write(0x200ff00,struct.pack('<2Ii',0x300f100,0x3001000,event))
                self.cpu.reg_write(self.r.UC_X86_REG_ESP,0x200ff00)
                self.remote_calls = []
                self.cpu.emu_start(0x62a540,0x300f100,count=10000)
                if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
                    raise ValueError('Original remote state did not return')
                result.append((bool(self.cpu.mem_read(0x3001099,1)[0]),tuple(self.remote_calls)))
        finally:
            self.capture_remote = False
        return result

    def state_playback(self,states,requests,factor=1.):
        """Original single-clip state selection/restart and playback settings."""
        if not 1 <= len(states) <= 8:
            raise ValueError('Expected bounded synthetic clip states')
        self.cpu.mem_write(0x3001000,bytes(0xb000))
        for offset,value in ((0x11c,0x3002000),(0x120,0x3003000),(0xf8,0x3004000)):
            self.word(0x3001000+offset,value)
        self.cpu.mem_write(0x3001144,struct.pack('<f',factor))
        self.word(0x3002008,0x3005000)
        self.word(0x300200c,0x3005000+68*len(states))
        self.word(0x3003014,1)
        self.word(0x3004000,0xffffffff)
        self.word(0x3004008,0xffffffff)
        self.state_clip_ids = {}
        for index,(name,speed,loop,flags) in enumerate(states):
            state,clip,string = 0x3005000+68*index,0x3006000+56*index,0x300700c+index*0x100
            self.word(string-12,len(name))
            self.cpu.mem_write(string,name+b'\0')
            self.state_clip_ids[string] = 100+index
            self.word(state+8,int(loop))
            self.word(state+0x24,flags)
            self.word(state+0x2c,clip)
            self.word(state+0x30,clip+56)
            self.word(clip,string)
            self.cpu.mem_write(clip+4,struct.pack('<f',speed))
        result = []
        self.capture_state = True
        try:
            for state,flags,finished,time in requests:
                self.state_calls,self.state_finished,self.state_time = [],finished,time
                self.cpu.mem_write(0x2000000,bytes(0x10000))
                self.cpu.mem_write(0x200ff00,struct.pack('<6I',0x300f100,0x3001000,0,state,flags,0xffffffff))
                self.cpu.reg_write(self.r.UC_X86_REG_ESP,0x200ff00)
                self.cpu.emu_start(0x5d5800,0x300f100,count=10000)
                if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x300f100:
                    raise ValueError('Original clip state did not return')
                result.append((tuple(self.state_calls),self.state_time))
        finally:
            self.capture_state = False
        return result

    def clip_bounds(self,start,end):
        """Original PlayAnimationOnController's clip-bound conversion block."""
        self.cpu.mem_write(0x2000000,bytes(0x10000))
        self.cpu.mem_write(0x3001000,bytes(0x3000))
        self.word(0x3001004,0x3002000)
        self.cpu.mem_write(0x3002000,struct.pack('<3I',0,start,end))
        frame = 0x200ff00
        self.word(frame+0x10,0)
        self.word(0x200fe00+0x18,0x3001000)
        for register,value in ((self.r.UC_X86_REG_EBP,frame),(self.r.UC_X86_REG_ESP,0x200fe00),
                               (self.r.UC_X86_REG_EBX,0x113e340),(self.r.UC_X86_REG_EAX,0x3001000),
                               (self.r.UC_X86_REG_EDI,0x3003000)):
            self.cpu.reg_write(register,value)
        self.cpu.emu_start(0x2798ed,0x27998a,count=1000)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP) != 0x27998a:
            raise ValueError('Original clip-bound conversion did not reach its boundary')
        return struct.unpack('<3i',self.cpu.mem_read(0x3003004,12))
