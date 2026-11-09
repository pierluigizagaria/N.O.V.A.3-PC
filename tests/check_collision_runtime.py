"""Opt-in original-to-distributed collision pose parity; temporary numeric data."""
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/analysis'))
from animation_metadata import clips
from authority_data import animation_clip_start, numeric_template, ragdoll_settings, remote_player, state_set
from hitbox_metadata import Animation, Skeleton, actor_resource, aim_rotations, collision_regions, compose, matrix
from world_data import technical_resources, template_index


def main():
    library=ROOT/'local/input/lib/x86/libNOVA3_neon.so'
    archives=sorted((ROOT/'local/game/obb').glob('*.obb'))
    if os.environ.get('NOVA_REAL_TESTS')!='1' or not library.is_file() or not archives:
        print('Original collision runtime comparison skipped (opt-in).')
        return
    source=json.loads((ROOT/'server/data/authority.json').read_text())['collision']
    gol=technical_resources(archives)['gol.bin']
    index=template_index(gol,ROOT/'build/win32/tests/Release/native-world-reader-tests.exe',library)
    states={s[0]:s for s in numeric_template(gol,index,1061,14,state_set)[1]}
    remote=numeric_template(gol,index,1002,44,remote_player)
    parts=numeric_template(gol,index,4550,140,ragdoll_settings)[0][0][1]
    skeleton=Skeleton(actor_resource(archives,b'mp_marine_mesh.bdae',False))
    animation=Animation(actor_resource(archives,b'mp_marine_anims.bdae',True))
    clip_times=clips(io.BytesIO(animation.data),0,len(animation.data))
    names=[b'Walk',b'WalkLeft',b'WalkBack',b'WalkRight',b'Run',b'RunLeft',b'RunBack',b'RunRight']
    supported={w['type_id'] for w in source['weapons']}
    for fields,_,actions in remote[2]:
        if fields[0] in supported:
            names.extend(name for name in actions if name not in names)
    aim=[skeleton.names[n] for n in (b'Bip01',b'Bip01_Spine',b'Bip01_Spine1',b'Bip01_Neck',b'Bip01_L_Clavicle',b'Bip01_R_Clavicle')]
    from original_pose_oracle import OriginalPoseOracle
    oracle=OriginalPoseOracle(library)
    queries=[]
    bind=skeleton.pose()
    for motion,name in enumerate(names):
        state=states[name];raw,end=clip_times[state[4][0][0]];start=animation_clip_start(raw)
        for fraction in (0.,.17,.51,.93):
            elapsed=int((end-start-1)*fraction)
            for yaw,pitch in ((0.,0.),(45.,60.),(-120.,-60.)):
                local=list(skeleton.local_pose(animation,start+elapsed,(b'Bip01',start,state[3][6])))
                rotations=aim_rotations([local[i][1] for i in aim],pitch)
                for i,q in zip(aim,rotations):
                    p,_,scale=local[i];local[i]=(p,q,scale)
                angle=math.radians(yaw)*.5
                root=matrix((3.,5.,7.),(0.,0.,math.sin(angle),math.cos(angle)),(1.,1.,1.))
                pose=[]
                for node,values in zip(skeleton.nodes,local):
                    pose.append(compose(root if node.parent<0 else pose[node.parent],matrix(*values)))
                regions=[]
                for part,region in zip(parts,source['regions']):
                    node=skeleton.names[part[1][0]]
                    values=[v for i,column in enumerate(pose[node]) for v in (*column,float(i==3))]
                    radius,extension=part[2][0],part[2][5]
                    cylinder=region['half_length']*2-extension*2
                    p,q=oracle.body_transform(values,part[0][0],radius,extension,cylinder)
                    x,y,z,w=q;axis=(2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w))
                    half=region['half_length']
                    regions.append({'part_id':region['part'],'damage_multiplier':region['damage_multiplier'],
                                    'radius_world_units':region['radius'],
                                    'axis_start':tuple(p[i]+axis[i]*half for i in range(3)),
                                    'axis_end':tuple(p[i]-axis[i]*half for i in range(3))})
                vector=lambda v:dict(zip(('X','Y','Z'),v))
                queries.append({'Motion':motion,'Time':elapsed,'Position':[3,5,7],'Yaw':yaw,'Pitch':pitch,
                    'Regions':[{'PartID':r['part_id'],'DamageMultiplier':r['damage_multiplier'],
                                'Volume':{'A':vector(r['axis_start']),'B':vector(r['axis_end']),'Radius':r['radius_world_units']}} for r in regions]})
    runtime=ROOT/'build/win32/tests/runtime'
    runtime.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='collision-',dir=runtime) as directory:
        fixture=Path(directory)/'poses.json';fixture.write_text(json.dumps(queries),encoding='utf-8')
        env=os.environ.copy();env['NOVA_COLLISION_FIXTURE']=str(fixture)
        cache=ROOT/'local/tools/go-cache'
        env.update(GOCACHE=str(cache/'build'),GOMODCACHE=str(cache/'modules'),GOPATH=str(cache/'path'),
                   GOTOOLCHAIN='local',GOPROXY='off',GOSUMDB='off',CGO_ENABLED='0')
        go=json.loads((ROOT/'local/tools/go-toolchain.json').read_text(encoding='utf-8-sig'))['go']
        subprocess.run([go,'test','./internal/collision','-run','^TestReducedCollisionOriginalFixture$','-v','-count=1'],cwd=ROOT/'server',env=env,check=True)


if __name__=='__main__':
    main()
