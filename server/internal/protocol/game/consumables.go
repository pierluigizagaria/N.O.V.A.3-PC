package game

const RequestConsumable byte = 161
const ActivateConsumable byte = 162

type Consumable struct{ Actor, Type byte }

func ReadConsumable(m []byte) (Consumable, error) {
	if len(m) != 3 || (m[0] != RequestConsumable && m[0] != ActivateConsumable) || m[1] >= 12 || m[2] >= 9 {
		return Consumable{}, ErrPacket
	}
	return Consumable{m[1], m[2]}, nil
}

func (v Consumable) Message(kind byte) ([]byte, error) {
	m := []byte{kind, v.Actor, v.Type}
	_, err := ReadConsumable(m)
	return m, err
}
