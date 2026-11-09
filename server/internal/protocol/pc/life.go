package pc

import "encoding/binary"

// Life is an explicit server declaration, not a client snapshot proposal.
// Repeat the current roster before snapshots. Duplicate declarations never
// reset interpolation; old generations cannot revive an earlier life.
type Life struct {
	Slot       byte
	Generation uint32
}

func EncodeLives(records []Life) ([]byte, error) {
	if len(records) == 0 || len(records) > MaxPlayers {
		return nil, ErrPacket
	}
	body := make([]byte, 1+5*len(records))
	body[0] = byte(len(records))
	for i, life := range records {
		body[1+5*i] = life.Slot
		binary.BigEndian.PutUint32(body[2+5*i:], life.Generation)
	}
	if _, err := DecodeLives(body); err != nil {
		return nil, err
	}
	return body, nil
}

func DecodeLives(body []byte) ([]Life, error) {
	if len(body) == 0 || body[0] == 0 || body[0] > MaxPlayers || len(body) != 1+int(body[0])*5 {
		return nil, ErrPacket
	}
	records := make([]Life, int(body[0]))
	var slots uint16
	for i := range records {
		life := Life{Slot: body[1+5*i], Generation: binary.BigEndian.Uint32(body[2+5*i:])}
		if life.Slot >= MaxPlayers || life.Generation == 0 || slots&(1<<life.Slot) != 0 {
			return nil, ErrPacket
		}
		slots |= 1 << life.Slot
		records[i] = life
	}
	return records, nil
}
