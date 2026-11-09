package native

import (
	"bytes"
	"errors"
	"log"
	"strings"
	"testing"
	"time"
)

func TestUDPDiagnosticsBoundAlternatingErrors(t *testing.T) {
	var output bytes.Buffer
	l := log.New(&output, "", 0)
	d := udpErrorLog{}
	now := time.Unix(100, 0)
	for i := 0; i < 1000; i++ {
		d.record(l, 1, errors.New([]string{"type=46", "type=54"}[i%2]), now)
	}
	if strings.Count(output.String(), "\n") != 2 {
		t.Fatal("alternating errors flooded log")
	}
	d.record(l, 1, errors.New("type=46"), now.Add(30*time.Second))
	if strings.Count(output.String(), "\n") != 3 || !strings.Contains(output.String(), "rejections=999") {
		t.Fatal(output.String())
	}
}
