package lobby

import (
	"bytes"
	"io"
	"testing"
)

type fragmentReader struct{ io.Reader }

func (r fragmentReader) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return r.Reader.Read(p)
}

type shortWriter struct{ bytes.Buffer }

func (w *shortWriter) Write(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return w.Buffer.Write(p)
}

func TestFragmentedAndCoalescedLobbyFrames(t *testing.T) {
	w := &shortWriter{}
	for _, f := range []Frame{{Opcode: 1, Payload: []byte{0, 0, 0, 7}}, {Opcode: 0x86, Payload: String("invented")}} {
		if err := Write(w, f); err != nil {
			t.Fatal(err)
		}
	}
	r := fragmentReader{bytes.NewReader(w.Bytes())}
	first, err := Read(r)
	if err != nil || first.Opcode != 1 || !bytes.Equal(first.Payload, []byte{0, 0, 0, 7}) {
		t.Fatal(first, err)
	}
	second, err := Read(r)
	if err != nil || second.Opcode != 0x86 || !bytes.Equal(second.Payload, String("invented")) {
		t.Fatal(second, err)
	}
	for _, bad := range [][]byte{{0, 0}, {0, 5, 1, 2, 0, 0, 0}, {0, 6, 2, 1, 0, 0, 0, 1}, {0, 8, 1, 2, 0}} {
		if _, err := Read(bytes.NewReader(bad)); err == nil {
			t.Fatal("invalid/truncated frame accepted")
		}
	}
}
