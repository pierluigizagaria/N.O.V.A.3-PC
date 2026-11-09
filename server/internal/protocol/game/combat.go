package game

import "encoding/binary"

const (
	DamagePlayer byte = 21
	HitPlayer    byte = 22
	HitObject    byte = 23
	PlayerDeath  byte = 29
)

// Damage is the complete victim notification, not a client-reported hit.
// Flags bit0 selects 1/32 damage units; bit1 marks target part3 and bit2
// corresponds to native CDamage source1. Kind and Direction share one byte.
// Actor/object IDs retain the original byte namespace; the simulation must
// resolve and authorize them before constructing a server notification.
type Damage struct {
	Attacker, Victim, Flags, Amount, Kind, Direction, HitEffect byte
}

func (v Damage) Value() float32 {
	if v.Flags&1 != 0 {
		return float32(v.Amount) / 32
	}
	return float32(v.Amount)
}

func (v Damage) Message() ([]byte, error) {
	if v.Flags > 7 || v.Kind > 15 || v.Direction > 15 {
		return nil, ErrPacket
	}
	return []byte{DamagePlayer, v.Attacker, v.Victim, v.Flags, v.Amount, v.Kind<<4 | v.Direction, v.HitEffect}, nil
}

func ReadDamage(m []byte) (Damage, error) {
	if len(m) != 7 || m[0] != DamagePlayer {
		return Damage{}, ErrPacket
	}
	v := Damage{m[1], m[2], m[3], m[4], m[5] >> 4, m[5] & 15, m[6]}
	if _, err := v.Message(); err != nil {
		return Damage{}, err
	}
	return v, nil
}

// Hit is the compact observer notification. Object hits include an unscaled
// damage byte; player hits carry only the target-part code. Neither replaces
// the complete victim notification, nor establishes server hit authority.
type Hit struct {
	Object                         bool
	Attacker, Victim, Amount, Part byte
}

func (v Hit) Message() ([]byte, error) {
	if v.Object {
		return []byte{HitObject, v.Attacker, v.Victim, v.Amount, v.Part}, nil
	}
	if v.Amount != 0 {
		return nil, ErrPacket
	}
	return []byte{HitPlayer, v.Attacker, v.Victim, v.Part}, nil
}

func ReadHit(m []byte) (Hit, error) {
	if len(m) == 4 && m[0] == HitPlayer {
		return Hit{Attacker: m[1], Victim: m[2], Part: m[3]}, nil
	}
	if len(m) == 5 && m[0] == HitObject {
		return Hit{true, m[1], m[2], m[3], m[4]}, nil
	}
	return Hit{}, ErrPacket
}

// Death is the original eight-byte server command. Weapon is the native type
// ID, not LaunchBullet's wire index. Flags preserve four native booleans (bit0
// marks target part3); Source is CDamage+0xc. Part/Direction share the last byte.
// Decoded field layout is separate from still-incomplete kill/scoring rules.
type Death struct {
	Victim, Attacker               byte
	Weapon                         uint16
	Flags, Source, Part, Direction byte
}

func (v Death) Message() ([]byte, error) {
	if v.Flags > 15 || v.Part > 15 || v.Direction > 15 {
		return nil, ErrPacket
	}
	m := []byte{PlayerDeath, v.Victim, v.Attacker, 0, 0, v.Flags, v.Source, v.Part<<4 | v.Direction}
	binary.BigEndian.PutUint16(m[3:], v.Weapon)
	return m, nil
}

func ReadDeath(m []byte) (Death, error) {
	if len(m) != 8 || m[0] != PlayerDeath {
		return Death{}, ErrPacket
	}
	v := Death{m[1], m[2], binary.BigEndian.Uint16(m[3:]), m[5], m[6], m[7] >> 4, m[7] & 15}
	if _, err := v.Message(); err != nil {
		return Death{}, err
	}
	return v, nil
}
