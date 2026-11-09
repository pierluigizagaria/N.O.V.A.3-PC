package game

import "encoding/binary"

const (
	PlayerLoadout     byte = 33
	ChangeWeapon      byte = 34
	AimWeapon         byte = 36
	FireStop          byte = 55
	ChangePlayerState byte = 137
	ReloadState       byte = 3
)

// WeaponAction preserves the native integer argument. The match owner must
// resolve it against the actor's equipped weapons/fire modes before execution.
type WeaponAction struct {
	Actor byte
	Value int32
}

func (a WeaponAction) Message(kind byte) ([]byte, error) {
	if a.Actor >= 12 || (kind != ChangeWeapon && kind != FireStop) {
		return nil, ErrPacket
	}
	m := []byte{kind, a.Actor, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(m[2:], uint32(a.Value))
	return m, nil
}

func ReadWeaponAction(m []byte) (WeaponAction, error) {
	if len(m) != 6 || (m[0] != ChangeWeapon && m[0] != FireStop) || m[1] >= 12 {
		return WeaponAction{}, ErrPacket
	}
	return WeaponAction{m[1], int32(binary.BigEndian.Uint32(m[2:]))}, nil
}

type Aim struct {
	Actor   byte
	Enabled bool
}

func (a Aim) Message() ([]byte, error) {
	if a.Actor >= 12 {
		return nil, ErrPacket
	}
	value := a.Actor
	if a.Enabled {
		value |= 0x80
	}
	return []byte{AimWeapon, value}, nil
}

func ReadAim(m []byte) (Aim, error) {
	if len(m) != 2 || m[0] != AimWeapon || m[1]&0x7f >= 12 {
		return Aim{}, ErrPacket
	}
	return Aim{m[1] & 0x7f, m[1]&0x80 != 0}, nil
}

// PlayerState is an intent, not an authoritative transition. In particular,
// StartReload emits state 3; the match must check ammo and action timing itself.
type PlayerState struct{ Actor, State byte }

func (s PlayerState) Message() ([]byte, error) {
	if s.Actor >= 12 || s.State > 15 {
		return nil, ErrPacket
	}
	return []byte{ChangePlayerState, s.Actor<<4 | s.State}, nil
}

func ReadPlayerState(m []byte) (PlayerState, error) {
	if len(m) != 2 || m[0] != ChangePlayerState || m[1]>>4 >= 12 {
		return PlayerState{}, ErrPacket
	}
	return PlayerState{m[1] >> 4, m[1] & 15}, nil
}
