"""Reduced numeric pickup rules; original templates remain private inputs."""
from authority_data import numeric_template, weapon_template, flag
from world_data import template_component, object_types, level
from hitbox_metadata import Skeleton, actor_resource


def pickup(reader):
    weapon, power = reader.integer(), reader.integer()
    flag(reader)
    ammo_only = flag(reader)
    ammo, radius, respawn = reader.integer(), reader.floats(1)[0], reader.integer()
    reader.string()
    flag(reader); flag(reader)
    return {'weapon_type_id':max(weapon,0), 'power_type_id':max(power,0),
            'ammo':ammo, 'ammo_only':ammo_only, 'radius_world_units':radius,
            'respawn_ms':respawn*1000}


def pickup_metadata(gol, index, resources, weapon_ids, archives):
    drops = []
    for weapon in sorted(weapon_ids):
        prefab = weapon_template(gol,index,weapon)[0x54]
        if (prefab,74) in index:
            rule = numeric_template(gol,index,prefab,74,pickup)
            if rule['weapon_type_id'] != weapon:
                raise ValueError('Death pickup disagrees with weapon')
            drops.append(rule)
    authored,spawn_zones = [],[]
    types = object_types(gol)
    resource = template_component(gol,index,122,1)[0]
    skeleton = Skeleton(actor_resource(archives,resource,True))
    for map_id,(filename,_) in enumerate(template_component(gol,index,1040,93)):
        zones = []
        for offset,kind,instance in level(resources[filename.decode('ascii').casefold()+'.lvc'],types):
            while zones and offset >= zones[-1][1]:
                zones.pop()
            if kind == 62:
                zone,_,end = instance
                zones.append((zone.identifier,end))
                continue
            if kind == 108:
                if not zones:
                    raise ValueError('Spawn has no native zone')
                spawn_zones.append({'map':map_id,'spawn_id':instance.placement.identifier,'zone':zones[-1][0]})
                continue
            if kind != 122:
                continue
            if not zones:
                raise ValueError('Pickup has no native zone')
            zone = zones[-1][0]
            base = instance[0]
            links = instance[145]
            initially_enabled = [i for i,(_,_,flags) in enumerate(links) if flags[0]]
            if len(initially_enabled) != 1:
                raise ValueError('Ambiguous initial pickup child')
            choices = []
            for child,binding,_ in links:
                # StartObject falls back to the parent's root when its named
                # attachment is absent. That is the supported pedestal asset.
                if binding in skeleton.names or base.scale != (1.,1.,1.):
                    raise ValueError('Pickup attachment needs a derived transform')
                rule = numeric_template(gol,index,child,74,pickup)
                if rule['weapon_type_id'] <= 0 or rule['power_type_id']:
                    raise ValueError('Unsupported authored pickup')
                choices.append(rule)
            authored.append({'map':map_id,'object_id':base.identifier,'zone':zone,
                             'position':base.position,'enabled':bool(base.flags[0]),
                             'modes':instance[121],'initial_choice':initially_enabled[0],'choices':choices})
    return authored,drops,spawn_zones
