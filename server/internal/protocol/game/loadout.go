package game

// Loadout preserves the original one-based selections, in wire order:
// perk1, perk2, primary, primary addons1/2, secondary, secondary addons1/2, item.
// Zero means empty. The final bool is MPLoadoutComponent::HasJetpack.
type Loadout struct {
	Actor      byte
	Selections [9]byte
	Jetpack    bool
}

func (v Loadout) Message() ([]byte, error) {
	m := make([]byte, 12)
	m[0], m[1] = PlayerLoadout, v.Actor
	copy(m[2:], v.Selections[:])
	if v.Jetpack {
		m[11] = 1
	}
	if _, err := ReadLoadout(m); err != nil {
		return nil, err
	}
	return m, nil
}

func ReadLoadout(m []byte) (Loadout, error) {
	if len(m) != 12 || m[0] != PlayerLoadout || m[1] >= 12 || m[11] > 1 {
		return Loadout{}, ErrPacket
	}
	v := Loadout{Actor: m[1], Jetpack: m[11] != 0}
	copy(v.Selections[:], m[2:11])
	for _, selection := range v.Selections {
		// Original beacon readers sign-extend these bytes before table lookup.
		if selection >= 128 {
			return Loadout{}, ErrPacket
		}
	}
	return v, nil
}

func (p Player) Loadout() (Loadout, error) {
	m := make([]byte, 12)
	m[0], m[1] = PlayerLoadout, p.Slot
	copy(m[2:], p.Beacon[4:14])
	return ReadLoadout(m)
}

func (p *Player) SetLoadout(v Loadout) {
	copy(p.Beacon[4:13], v.Selections[:])
	p.Beacon[13] = 0
	if v.Jetpack {
		p.Beacon[13] = 1
	}
}
