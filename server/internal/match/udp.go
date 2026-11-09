package match

import (
	"context"
	"errors"
	"net"
	"net/netip"
	"time"
)

type datagramSession interface {
	Handle(netip.AddrPort, []byte, time.Time) ([]Delivery, error)
	Tick(time.Time) ([]Delivery, error)
	Close()
}

// Run owns a plain compatibility socket. Plain original traffic is restricted
// to loopback; this is not the transport for a remote community deployment.
func Run(ctx context.Context, conn *net.UDPConn, s *Session) error {
	return run(ctx, conn, s, true)
}

// RunAuthenticated accepts real remote endpoints only through the account-bound
// encrypted transport. It is the VPS/LAN-capable runner; control-plane key
// delivery must use authenticated TLS. The public room hub uses this transport
// for the explicitly limited connection/movement preview.
func RunAuthenticated(ctx context.Context, conn *net.UDPConn, s *Bound) error {
	return run(ctx, conn, s, false)
}

// RunMultiplexed shares the same absolute 30 Hz scheduler and cancellation
// behavior across authenticated room allocations on one public UDP port.
func RunMultiplexed(ctx context.Context, conn *net.UDPConn, s interface {
	Handle(netip.AddrPort, []byte, time.Time) ([]Delivery, error)
	Tick(time.Time) ([]Delivery, error)
	Close()
}) error {
	return run(ctx, conn, s, false)
}

func run(ctx context.Context, conn *net.UDPConn, s datagramSession, loopbackOnly bool) error {
	defer conn.Close()
	defer s.Close()
	if loopbackOnly && !conn.LocalAddr().(*net.UDPAddr).AddrPort().Addr().IsLoopback() {
		return ErrPeer
	}
	stop := context.AfterFunc(ctx, func() { conn.Close() })
	defer stop()
	send := func(out []Delivery) error {
		for _, v := range out {
			if loopbackOnly && !v.To.Addr().IsLoopback() {
				return ErrPeer
			}
			if _, err := conn.WriteToUDPAddrPort(v.Data, v.To); err != nil {
				if ctx.Err() != nil {
					return nil
				}
				return err
			}
		}
		return nil
	}
	// Read a complete UDP payload before applying the protocol's smaller bound;
	// Windows otherwise reports WSAEMSGSIZE for a hostile oversized datagram.
	buf := make([]byte, 65535)
	clock := newCadence(time.Now())
	for {
		if ctx.Err() != nil {
			return nil
		}
		if err := conn.SetReadDeadline(clock.deadline()); err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		n, from, err := conn.ReadFromUDPAddrPort(buf)
		if ctx.Err() != nil {
			return nil
		}
		now := time.Now()
		if err == nil {
			out, handleErr := s.Handle(from, buf[:n], now)
			if errors.Is(handleErr, ErrTimeout) || errors.Is(handleErr, ErrWindow) {
				return handleErr
			}
			if handleErr == nil {
				if err = send(out); err != nil {
					return err
				}
			}
		} else if e, ok := err.(net.Error); !ok || !e.Timeout() {
			return err
		}
		if !clock.due(now) {
			continue
		}
		out, tickErr := s.Tick(now)
		if tickErr != nil {
			return tickErr
		}
		if err = send(out); err != nil {
			return err
		}
	}
}
