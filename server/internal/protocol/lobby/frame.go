// Package lobby implements the original length-prefixed lobby stream.
package lobby

import (
	"encoding/binary"
	"errors"
	"io"
)

const MaxFrame = 4096

type Frame struct {
	Opcode  uint32
	Payload []byte
}

func Read(r io.Reader) (Frame, error) {
	var size [2]byte
	if _, err := io.ReadFull(r, size[:]); err != nil {
		return Frame{}, err
	}
	n := int(binary.BigEndian.Uint16(size[:]))
	if n < 6 || n > MaxFrame {
		return Frame{}, errors.New("invalid lobby frame size")
	}
	body := make([]byte, n)
	if _, err := io.ReadFull(r, body); err != nil {
		return Frame{}, err
	}
	if body[0] != 1 || body[1] != 2 {
		return Frame{}, errors.New("unsupported lobby framing")
	}
	return Frame{Opcode: binary.BigEndian.Uint32(body[2:6]), Payload: body[6:]}, nil
}

func Write(w io.Writer, f Frame) error {
	if len(f.Payload)+6 > MaxFrame {
		return errors.New("lobby payload too large")
	}
	data := make([]byte, len(f.Payload)+8)
	binary.BigEndian.PutUint16(data, uint16(len(data)-2))
	data[2], data[3] = 1, 2
	binary.BigEndian.PutUint32(data[4:8], f.Opcode)
	copy(data[8:], f.Payload)
	for len(data) > 0 {
		n, err := w.Write(data)
		if err != nil {
			return err
		}
		if n <= 0 || n > len(data) {
			return io.ErrShortWrite
		}
		data = data[n:]
	}
	return nil
}

func String(text string) []byte {
	out := make([]byte, len(text)+2)
	binary.BigEndian.PutUint16(out, uint16(len(text)))
	copy(out[2:], text)
	return out
}
