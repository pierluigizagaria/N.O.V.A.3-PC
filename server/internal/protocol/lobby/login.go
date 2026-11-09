package lobby

import (
	"encoding/binary"
	"errors"
)

// Login retains the original identity fields separately. GGI and version are
// protocol metadata, not substitutes for account authentication.
type Login struct {
	GGI     uint32
	UID     string
	Mode    byte
	Version uint32
}

func ParseLogin(payload []byte) (Login, error) {
	if len(payload) < 11 {
		return Login{}, errors.New("short lobby login")
	}
	n := int(binary.BigEndian.Uint16(payload[4:6]))
	if n == 0 || n > 99 || len(payload) != 11+n {
		return Login{}, errors.New("invalid lobby UID length")
	}
	return Login{GGI: binary.BigEndian.Uint32(payload[:4]), UID: string(payload[6 : 6+n]), Mode: payload[6+n], Version: binary.BigEndian.Uint32(payload[7+n:])}, nil
}

func LoginAccepted(id, name string) (Frame, error) {
	if len(id) == 0 || len(id) > 16 || len(name) == 0 || len(name) > 16 {
		return Frame{}, errors.New("invalid native lobby identity")
	}
	body := make([]byte, 4)
	body = append(body, String(id)...)
	body = append(body, String(name)...)
	return Frame{Opcode: LoginResponse, Payload: body}, nil
}
