package native

import (
	"log"
	"time"
)

// Repeated, alternating unsupported commands must not flood the operator's
// console. Keep bounded distinct diagnostics and one periodic count thereafter.
type udpErrorLog struct {
	seen     map[string]bool
	next     time.Time
	repeated uint64
}

func (d *udpErrorLog) record(logger *log.Logger, room uint32, err error, now time.Time) {
	if logger == nil || err == nil {
		return
	}
	if d.seen == nil {
		d.seen = make(map[string]bool)
		d.next = now.Add(30 * time.Second)
	}
	message := err.Error()
	if !d.seen[message] && len(d.seen) < 16 {
		d.seen[message] = true
		logger.Printf("native match room=%d UDP rejected: %s", room, message)
		return
	}
	d.repeated++
	if !now.Before(d.next) {
		logger.Printf("native match room=%d repeated UDP rejections=%d last=%s", room, d.repeated, message)
		d.repeated = 0
		d.next = now.Add(30 * time.Second)
	}
}
