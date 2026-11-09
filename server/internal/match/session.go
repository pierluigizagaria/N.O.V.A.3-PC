// Package match owns authenticated loading/synchronization and the explicitly
// limited connection/movement preview. Full gameplay authority remains pending.
package match

import (
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"net/netip"
	"sync"
	"time"

	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/collision"
	"nova3.test/backend/internal/protocol/game"
)

type Phase string

const (
	Accepting     Phase = "accepting"
	WaitingRoster Phase = "waiting-roster"
	Loading       Phase = "loading"
	Loaded        Phase = "loaded"
	Synchronizing Phase = "synchronizing"
	Synchronized  Phase = "synchronized"
	Playing       Phase = "playing"
	Finishing     Phase = "finishing"
	Finished      Phase = "finished"
	Closed        Phase = "closed"
	Failed        Phase = "failed"
)

var (
	ErrState       = errors.New("invalid match session transition")
	ErrPeer        = errors.New("endpoint is not reserved for this match")
	ErrTimeout     = errors.New("match session deadline expired")
	ErrUnsupported = errors.New("native operation is not implemented")
)

// Member is a reservation from a trusted owner, never an unauthenticated packet
// or operator file. Exact endpoints bind UDP packets to that reservation. The
// production account-to-endpoint proof remains an integration prerequisite;
// names and source IP alone must never be treated as credentials.
type Member struct {
	Account, Name string
	Endpoint      netip.AddrPort
}
type Options struct {
	ConsumeConsumable func(string, byte) (bool, error)
	Authority         *authority.Definitions
	Members           []Member
	Details           game.Details
	Loading           game.Loading
	PrimaryWeapons    map[byte]uint16 // One-based native selections -> presentation type IDs.
	SecondaryWeapons  map[byte]uint16
	Addons            map[byte]byte // One-based catalogue selections -> native add-on identities.
	Items             map[byte]uint16
	Loadouts          map[string]LoadoutPolicy
}
type Delivery struct {
	To   netip.AddrPort
	Data []byte
}
type Snapshot struct {
	Spawns                            [12]uint32
	Phase                             Phase
	Reserved, Joined, Loaded, Pending int
	Heartbeats                        int
	Active                            int
	PositionInputs, PositionChanges   [12]uint64
	SnapshotBatches                   uint64
	Combat                            [12]CombatSnapshot
}

// Bounded diagnostics for the current combat playtest, with no player secrets.
type CombatSnapshot struct {
	ConsumablesUsed                                    [9]uint32
	Weapon                                             uint16
	Magazine                                           int32
	ShotsReceived                                      uint64
	Shots, Hits                                        uint32
	Health                                             float32
	SpawnPending                                       bool
	RespawnRequests                                    uint64
	Respawns                                           uint64
	LastRejectedAt                                     byte // authorizeShot stage; zero means its last intent passed.
	ThrowsReceived, ThrowsAccepted                     uint64
	ThrowRejectedAt                                    byte
	MountType                                          int32
	MountHealth                                        float32
	ShotWeapon, ShotFlags, ShotSource, ShotSourceIndex byte
	Trace                                              string
}
type peer struct {
	consumables                     consumableState
	member                          Member
	player                          game.Player
	admission                       game.Player
	receivedBodies                  map[uint32][32]byte
	loadoutSequence                 uint32
	haveLoadoutSequence             bool
	channel                         channel
	discovered, joined, loaded      bool
	haveSpawn                       bool
	clock                           clockSync
	ping                            heartbeat
	sceneReady                      bool
	roundSpawnPending               bool
	weapon                          uint16
	weapons                         map[uint16]*weaponState
	throwStock                      map[int32]int32
	throwEquipment                  map[int32]authority.Equipment
	lastThrow                       uint32
	haveThrow                       bool
	life                            lifeState
	respawnRequests, respawns       uint64
	collider                        *collision.Actor
	aiming                          bool
	zone                            *game.ZoneChange
	departed                        bool
	transportFailed                 bool
	lastMount                       *mountState
	lastMountUntil                  time.Time
	lastReceived                    time.Time
	lastPositionTime                uint32
	admitting                       Phase // Individual late-join stage; empty for initial members.
	admissionDeadline               time.Time
	nextChat                        time.Time
	positionInputs, positionChanges uint64
	shotIntents                     uint64
	shotReject                      byte
	shotDiagnostic                  game.Shot
	lastDischarge                   *dischargeHit
	throwIntents, throwAccepted     uint64
	throwReject                     byte
	chemicalUntil                   time.Time
	nextMelee                       time.Time
	mount                           *mountSeat
}
type Session struct {
	consumeConsumable func(string, byte) (bool, error)
	authority         *authority.Definitions
	mu                sync.Mutex
	snapshotBatches   uint64
	phase             Phase
	details           game.Details
	loading           game.Loading
	peers             []*peer
	retiredPeers      []*peer // Preserve results when a departed slot is reused.
	resultTime        time.Time
	byEndpoint        map[netip.AddrPort]*peer
	version           uint32
	deadline          time.Time
	endsAt            time.Time
	endGrace          time.Time
	round             roundState
	teamScores        [2]int32
	objectives        *objectiveState
	epoch             time.Time
	movement          *movement
	bullets           []bullet
	grenades          []thrownGrenade
	mounts            []*mountState
	pickups           []*pickupState
	nextPickupID      int32
	hitHistory        []hitFrame
}

func New(options Options, now time.Time) (*Session, error) {
	if len(options.Members) < 1 || len(options.Members) > 12 || int(options.Loading.Capacity) < len(options.Members) ||
		options.Details.Beacon[0] != options.Loading.Map || options.Details.Beacon[2] != options.Loading.Capacity ||
		options.Details.Beacon[3] != options.Loading.Mode {
		return nil, ErrState
	}
	if _, err := options.Details.Message(); err != nil {
		return nil, err
	}
	if _, err := options.Loading.Message(); err != nil {
		return nil, err
	}
	s := &Session{consumeConsumable: options.ConsumeConsumable, authority: options.Authority, phase: Accepting, details: options.Details, loading: options.Loading, deadline: now.Add(15 * time.Second), byEndpoint: make(map[netip.AddrPort]*peer)}
	accounts := make(map[string]bool)
	for i, m := range options.Members {
		if m.Account == "" || accounts[m.Account] || !m.Endpoint.IsValid() || !m.Endpoint.Addr().Is4() || m.Endpoint.Port() == 0 ||
			m.Endpoint.Addr().IsUnspecified() || m.Endpoint.Addr().IsMulticast() || m.Endpoint.Addr().String() == "255.255.255.255" || s.byEndpoint[m.Endpoint] != nil {
			return nil, ErrPeer
		}
		p := &peer{member: m, player: game.Player{Name: m.Name, Slot: byte(i)}, lastReceived: now}
		if _, err := p.player.Message(); err != nil {
			return nil, err
		}
		accounts[m.Account] = true
		s.peers = append(s.peers, p)
		s.byEndpoint[m.Endpoint] = p
	}
	return s, nil
}

func (s *Session) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	v := Snapshot{Phase: s.phase}
	v.SnapshotBatches = s.snapshotBatches
	for _, p := range s.peers {
		if s.movement != nil && p.haveSpawn {
			v.Spawns[p.player.Slot] = s.movement.spawns[p.player.Slot].ID
		}
		v.PositionInputs[p.player.Slot] = p.positionInputs
		v.PositionChanges[p.player.Slot] = p.positionChanges
		combat := CombatSnapshot{Weapon: p.weapon, ShotsReceived: p.shotIntents, Shots: p.life.shots, Hits: p.life.hits, Health: p.life.health, SpawnPending: p.roundSpawnPending, LastRejectedAt: p.shotReject}
		combat.ShotWeapon, combat.ShotFlags, combat.ShotSource, combat.ShotSourceIndex = p.shotDiagnostic.Weapon, p.shotDiagnostic.Flags, p.shotDiagnostic.SourceType, p.shotDiagnostic.SourceIndex
		if p.lastDischarge != nil {
			combat.Trace = p.lastDischarge.trace.String()
		}
		if p.mount != nil {
			combat.MountType = p.mount.vehicle.definition.TypeID
			combat.MountHealth = p.mount.vehicle.health
		}
		combat.RespawnRequests, combat.Respawns = p.respawnRequests, p.respawns
		combat.ThrowsReceived, combat.ThrowsAccepted, combat.ThrowRejectedAt = p.throwIntents, p.throwAccepted, p.throwReject
		if w := p.weapons[p.weapon]; w != nil {
			combat.Magazine = w.ammo.Snapshot().Magazine
		}
		combat.ConsumablesUsed = p.consumables.used
		v.Combat[p.player.Slot] = combat
		if p.departed {
			continue
		}
		v.Reserved++
		if p.joined {
			v.Joined++
		}
		if p.loaded {
			v.Loaded++
		}
		v.Pending += len(p.channel.pending)
		if p.ping.confirmed {
			v.Heartbeats++
		}
		if p.sceneReady {
			v.Active++
		}
	}
	return v
}

func (s *Session) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase != Failed {
		s.phase = Closed
	}
	for _, p := range s.peers {
		p.channel = channel{}
	}
}

func (s *Session) RequestLoading() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase != Accepting {
		return ErrState
	}
	for _, p := range s.peers {
		if !p.joined {
			return ErrState
		}
	}
	s.phase = WaitingRoster
	return nil
}

func (s *Session) emit(p *peer, body []byte, reliable bool, now time.Time, out *[]Delivery) error {
	if p.departed || p.transportFailed {
		return nil
	}
	b, err := p.channel.send(body, reliable, now)
	if err != nil {
		if errors.Is(err, ErrWindow) && (s.phase == Playing || s.phase == Finishing) && s.movement != nil {
			// Bound retires this endpoint on the next tick. A stalled receiver
			// cannot terminate the independently hosted match for its peers.
			p.transportFailed = true
			return nil
		}
		s.phase = Failed
		return err
	}
	*out = append(*out, Delivery{p.member.Endpoint, b})
	return nil
}

func (s *Session) rosterMessage() ([]byte, error) {
	players := make([]game.Player, 0, len(s.peers))
	for _, p := range s.peers {
		if p.joined && !p.departed {
			players = append(players, p.player)
		}
	}
	s.version++
	beacon := s.details.Beacon
	beacon[1] = byte(len(players))
	return game.PlayerList(s.version, beacon, players)
}

func (s *Session) roster(now time.Time, out *[]Delivery) error {
	b, err := s.rosterMessage()
	if err != nil {
		s.phase = Failed
		return err
	}
	for _, p := range s.peers {
		if p.joined {
			if err = s.emit(p, b, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}

func (s *Session) Handle(from netip.AddrPort, data []byte, now time.Time) ([]Delivery, error) {
	pkt, err := game.Decode(data)
	if err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase == Closed || s.phase == Failed {
		return nil, ErrState
	}
	p := s.byEndpoint[from]
	if p == nil || p.transportFailed {
		return nil, ErrPeer
	}
	if s.phase == Finishing || s.phase == Finished {
		return s.receiveEnding(p, pkt, now)
	}
	if s.phase != Playing && !now.Before(s.deadline) {
		s.phase = Failed
		return nil, ErrTimeout
	}
	// Exact accepted reliable retries remain ACKable after phase/equipment
	// transitions. A reused sequence with different commands is never a retry.
	digest := sha256.Sum256(data[game.HeaderSize:])
	duplicate := false
	if pkt.Header.Reliable {
		if previous, ok := p.receivedBodies[pkt.Header.Sequence]; ok {
			if previous != digest {
				return nil, game.ErrPacket
			}
			duplicate = true
		}
	}
	// Validate every new message before ACK/history or membership changes.
	var join *game.Player
	candidate := p.player
	loadoutBoundary := s.loadoutBoundary(p)
	phase := s.phase
	if p.admitting != "" {
		phase = p.admitting
	}
	staleEquipment := pkt.Header.Reliable && p.haveLoadoutSequence && pkt.Header.Sequence < p.loadoutSequence
	for _, m := range pkt.Messages {
		if duplicate {
			break
		}
		switch m[0] {
		case game.Discover:
			if phase != Accepting {
				return nil, ErrState
			}
		case game.ClientDetails:
			v, e := game.ReadPlayer(m)
			if e != nil {
				return nil, e
			}
			if !pkt.Header.Reliable || !p.discovered || v.Name != p.member.Name {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			v.Slot = p.player.Slot
			loadout, e := v.Loadout()
			if e != nil || !s.validLoadout(p, loadout) {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if p.joined && v != p.admission {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if !p.joined && phase != Accepting {
				return nil, ErrState
			}
			if join != nil && *join != v {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			join = &v
		case game.FinishedLoading:
			if !pkt.Header.Reliable || !p.joined || (phase != Loading && phase != Loaded && phase != Synchronizing && phase != Synchronized) {
				return nil, ErrState
			}
		case game.ClockReply:
			if !p.joined || (phase != Synchronizing && phase != Synchronized) || !p.clock.validSample(m[1]) {
				return nil, ErrState
			}
		case game.Ping:
			if !p.joined {
				return nil, ErrState
			}
		case game.Acknowledge:
			if !p.joined {
				return nil, ErrState
			}
		case game.PlayerLoadout:
			if !pkt.Header.Reliable || s.movement == nil || !p.joined || (s.phase != Loading && s.phase != Loaded && s.phase != Synchronizing && s.phase != Synchronized && s.phase != Playing) {
				return nil, ErrState
			}
			v, e := game.ReadLoadout(m)
			if e != nil || v.Actor != p.player.Slot || !s.validLoadout(p, v) {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if loadoutBoundary && (!p.haveLoadoutSequence || pkt.Header.Sequence > p.loadoutSequence) {
				candidate.SetLoadout(v)
			}
		case game.SceneReady, game.RequestPowerups:
			if !pkt.Header.Reliable || phase != Playing || s.movement == nil || !p.loaded {
				return nil, ErrState
			}
		case game.RequestRespawn:
			if !pkt.Header.Reliable || s.phase != Playing || s.movement == nil || !p.sceneReady {
				return nil, ErrState
			}
			if m[1] != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if !s.round.runningSince.IsZero() {
				loadoutBoundary = false
			}
		case game.RequestConsumable, game.ActivateConsumable:
			v, _ := game.ReadConsumable(m)
			if !pkt.Header.Reliable || s.phase != Playing || !p.sceneReady {
				return nil, ErrState
			}
			if v.Actor != p.player.Slot {
				return nil, ErrPeer
			}
		case game.ClientXP, game.ClientPurchasedInfo, game.StartPowerAnimation, game.DetonateMine,
			game.RequestEnterMachinegun, game.ExitMachinegun:
			// The online134 receiver sends its accumulated XP back as164.
			// Drain the original reliable notification without accepting client
			// scores as authority or letting it change another player's state.
			// Likewise, purchased-info163 is a client claim, not proof of
			// ownership. Account inventory remains the entitlement authority.
			// Native power49 (actor, animation) and mine51 (owner actor) share
			// the reliable queue with loadout33/request32. Authenticate and
			// drain them even after death; rejecting the whole bundle strands
			// the one-shot respawn request. Power/mine effects remain server
			// simulation work, never a trusted client relay.
			// Mounted-gun127/129 also queue actor intents, not server seat
			// grants. Drain them without blocking on-foot shots or spawns.
			if !pkt.Header.Reliable || s.phase != Playing || !p.sceneReady {
				return nil, ErrState
			}
			if m[1] != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
		case game.PresetChat:
			chat, e := game.ReadChat(m)
			if e != nil || s.phase != Playing || s.movement == nil || !p.sceneReady || !pkt.Header.Reliable {
				return nil, ErrState
			}
			if chat.Actor != p.player.Slot || !chat.Allowed(s.loading.Mode, p.player.Beacon[0]) {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
		case game.RequestSelfDamage:
			v, e := game.ReadSelfDamage(m)
			if e != nil || v.Actor != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if s.phase != Playing {
				return nil, ErrState
			}
			// Drain the native intent without trusting a client damage claim.
			// Falling/environmental authority is a separate simulation path.
		case game.RequestEnterMech, game.ExitMech, game.ExitVehicle:
			if !pkt.Header.Reliable || s.phase != Playing || !p.sceneReady {
				return nil, ErrState
			}
			if m[1] != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
		case game.MechContact:
			// MP_KillPlayerWithMech writes victim first, sender second. This is
			// only a contact claim; the server collision simulation owns damage.
			if !pkt.Header.Reliable || s.phase != Playing || !p.sceneReady || m[1] >= 12 || m[2] != p.player.Slot {
				return nil, fmt.Errorf("%w: mech contact slot=%d victim=%d sender=%d", ErrPeer, p.player.Slot, m[1], m[2])
			}
		case game.NativeFrameTime:
			// Native vehicle frames prefix their queue with83 (elapsed time).
			// It cannot replace the server clock, but must not reject the bundle.
			if s.phase != Playing || !p.sceneReady {
				return nil, ErrState
			}
		case game.VehicleImpact, game.VehicleDamageClaim:
			// This client collision impulse is not server damage authority.
			// Its target can be another actor; do not let it strand the reliable
			// queue containing seat release, grenade animation or respawn.
			if s.phase != Playing || !p.sceneReady || m[1] >= 12 || m[0] == game.VehicleDamageClaim && m[2] >= 12 {
				return nil, ErrState
			}
		case game.VehicleState, game.VehicleKinematics, game.VehicleShooterState, game.MechState:
			if s.phase != Playing || !p.sceneReady {
				return nil, ErrState
			}
			// Stale in-flight mounted traffic is drained after exit/death; it
			// cannot acquire a seat or change another vehicle's state.
		case game.ThrowGrenade, game.ThrowGrenadeWithSpeed, game.MeleeAttack:
			if !pkt.Header.Reliable || s.phase != Playing || s.movement == nil || !p.sceneReady {
				return nil, ErrState
			}
			if m[0] == game.MeleeAttack {
				v, _ := game.ReadMeleeRequest(m)
				if int(v.Victim) >= len(s.peers) || v.Victim == p.player.Slot || !s.peers[v.Victim].sceneReady {
					return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
				}
			} else if m[1] != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			// The execution pass below owns grenade/melee lifecycle and effects;
			// framing admission alone never grants client-reported damage.
		case game.ChangeZone, game.ChangeWeapon, game.AimWeapon, game.FireStop, game.ChangePlayerState, game.LaunchBullet,
			game.FireStopSound, game.JetpackStart, game.JetpackStop, game.RequestEnterVehicle:
			if s.phase != Playing || s.movement == nil {
				return nil, ErrState
			}
			// Authorize actor intents here; only the server simulation may produce
			// hit, damage, death or respawn notifications.
			actor := m[1]
			switch m[0] {
			case game.AimWeapon:
				actor &= 0x7f
			case game.ChangePlayerState:
				actor >>= 4
			case game.LaunchBullet:
				actor = m[7]
			}
			if actor != p.player.Slot {
				return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
			}
			if m[0] == game.ChangeZone {
				zone, _ := game.ReadZoneChange(m)
				// The admitted actor may also control its mount/proxy zones.
				// Map membership is verified; spatial authority remains pending.
				if !s.zoneObjectOwned(p, zone.Object, now) || !s.movement.zones[zone.Zone] {
					return nil, fmt.Errorf("%w: zone message slot=%d object=%d zone=%d known=%t", ErrPeer, p.player.Slot, zone.Object, zone.Zone, s.movement.zones[zone.Zone])
				}
			}
			if m[0] == game.ChangeWeapon && len(s.movement.primaryWeapons) != 0 {
				a, e := game.ReadWeaponAction(m)
				if e != nil || a.Value <= 0 || a.Value > 65535 || (!staleEquipment && !s.playerHasWeapon(candidate, uint16(a.Value)) && p.weapons[uint16(a.Value)] == nil && !s.temporaryWeapon(p, uint16(a.Value))) {
					return nil, fmt.Errorf("%w: message=%d slot=%d actor_byte=%d", ErrPeer, m[0], p.player.Slot, m[1])
				}
			}
		default:
			return nil, fmt.Errorf("%w: type=%d", ErrUnsupported, m[0])
		}
	}
	fresh, err := p.channel.receive(pkt.Header, now)
	if err != nil {
		return nil, err
	}
	p.lastReceived = now
	var out []Delivery
	if fresh {
		if pkt.Header.Reliable {
			if p.receivedBodies == nil {
				p.receivedBodies = make(map[uint32][32]byte)
			}
			p.receivedBodies[pkt.Header.Sequence] = digest
			for sequence := range p.receivedBodies {
				if p.channel.received-sequence >= 256 {
					delete(p.receivedBodies, sequence)
				}
			}
		}
		for _, m := range pkt.Messages {
			switch m[0] {
			case game.Discover:
				p.discovered = true
				d := s.details
				d.Beacon[1] = 0
				for _, other := range s.peers {
					if other.joined && !other.departed {
						d.Beacon[1]++
					}
				}
				b, _ := d.Message()
				if err = s.emit(p, b, false, now, &out); err != nil {
					return nil, err
				}
			case game.ClientDetails:
				if !p.joined {
					p.player = *join
					p.admission = *join
					s.assignTeam(p)
					if s.movement != nil {
						p.weapon = s.movement.primaryWeapons[p.player.Beacon[6]]
					}
					p.joined = true
					p.ping.next = now.Add(pingInterval)
					if p.admitting != "" {
						err = s.loadNewcomer(p, now, &out)
					} else {
						err = s.roster(now, &out)
					}
					if err != nil {
						return nil, err
					}
				}
			case game.FinishedLoading:
				p.loaded = true
				if p.admitting == Loading {
					p.admitting = Synchronizing
					p.admissionDeadline = now.Add(15 * time.Second)
				}
			case game.PlayerLoadout:
				v, _ := game.ReadLoadout(m)
				if err = s.applyLoadout(p, v, pkt.Header.Sequence, now, &out); err != nil {
					return nil, err
				}
			case game.ClockReply:
				p.clock.receive(m[1], binary.BigEndian.Uint32(m[2:]), s.elapsed(now))
			case game.Ping:
				p.ping.receive(binary.BigEndian.Uint32(m[1:]), now)
			case game.SceneReady:
				if err = s.activate(p, now, &out); err != nil {
					return nil, err
				}
			case game.RequestPowerups:
				// The original response also completes the round-clock handshake.
				if err = s.pickupScene(p, now, &out); err != nil {
					return nil, err
				}
				body, e := s.roundClock(now).Message()
				if e != nil {
					return nil, e
				}
				if err = s.emit(p, body, true, now, &out); err != nil {
					return nil, err
				}
			case game.RequestRespawn:
				p.respawnRequests++
				if err = s.spawnRound(p, now, &out); err != nil {
					return nil, err
				}
			case game.PresetChat:
				// ACK rapid repeats so the original reliable queue can drain;
				// preserve the native two-second presentation cooldown.
				if now.Before(p.nextChat) {
					continue
				}
				p.nextChat = now.Add(2 * time.Second)
				chat, _ := game.ReadChat(m)
				for _, other := range s.peers {
					if other != p && other.sceneReady && !other.departed && (chat.Team == 0 || s.teammates(p, other)) {
						if err = s.emit(other, m, true, now, &out); err != nil {
							return nil, err
						}
					}
				}
			case game.ChangeWeapon:
				if staleEquipment || len(s.movement.primaryWeapons) == 0 {
					continue
				}
				a, _ := game.ReadWeaponAction(m)
				if !s.temporaryWeapon(p, uint16(a.Value)) {
					p.selectWeapon(uint16(a.Value), now)
				}
				if err = s.present(p, m, true, now, &out); err != nil {
					return nil, err
				}
			case game.ChangeZone:
				zone, _ := game.ReadZoneChange(m)
				if p.zone != nil && !game.NewerZoneSequence(zone.Sequence, p.zone.Sequence) {
					continue
				}
				p.zone = &zone
				if err = s.present(p, m, true, now, &out); err != nil {
					return nil, err
				}
			case game.AimWeapon:
				if staleEquipment {
					continue
				}
				a, _ := game.ReadAim(m)
				p.aiming = a.Enabled
				action := 0
				if p.aiming {
					action = 1
				}
				s.collisionAction(p, action, now)
				if err = s.present(p, m, true, now, &out); err != nil {
					return nil, err
				}
			case game.FireStopSound, game.JetpackStart, game.JetpackStop:
				// Presentation events do not grant damage, fuel or physics authority.
				if err = s.present(p, m, pkt.Header.Reliable, now, &out); err != nil {
					return nil, err
				}
			case game.FireStop:
				if p.mount != nil && p.mount.vehicle.mech != nil {
					p.mount.vehicle.mech.StopFire(s.elapsed(now))
				}
				action := 0
				if p.aiming {
					action = 1
				}
				s.collisionAction(p, action, now)
				if err = s.present(p, m, false, now, &out); err != nil {
					return nil, err
				}
			case game.ChangePlayerState:
				if err = s.reload(p, m, now, &out); err != nil {
					return nil, err
				}
			case game.LaunchBullet:
				if err = s.shoot(p, m, now, &out); err != nil {
					return nil, err
				}
			case game.ThrowGrenade, game.ThrowGrenadeWithSpeed:
				if err = s.throwGrenade(p, m, now, &out); err != nil {
					return nil, err
				}
			case game.DetonateMine:
				if err = s.detonateMines(p, now, &out); err != nil {
					return nil, err
				}
			case game.RequestEnterVehicle, game.RequestEnterMachinegun, game.RequestEnterMech, game.ExitVehicle, game.ExitMachinegun, game.ExitMech,
				game.VehicleState, game.VehicleKinematics, game.VehicleShooterState, game.MechState:
				if err = s.mountIntent(p, m, pkt.Header.Reliable, now, &out); err != nil {
					return nil, err
				}
			case game.RequestConsumable, game.ActivateConsumable:
				if err = s.activateConsumable(p, m, now, &out); err != nil {
					return nil, err
				}
			case game.StartPowerAnimation:
				// Preparation17 must be followed by release1/5/13, including a
				// quick tap. A shared cooldown stranded the remote held-grenade pose.
				if !p.life.dead && !p.roundSpawnPending && (m[2] == 1 || m[2] == 5 || m[2] == 7 || m[2] == 17 || m[2] == 2 || m[2] == 13) {
					if a := s.collider(p, now); a != nil {
						a.Power(m[2], s.elapsed(now))
					}
					if err = s.present(p, m, true, now, &out); err != nil {
						return nil, err
					}
				}
			case game.MeleeAttack:
				if err = s.melee(p, m, now, &out); err != nil {
					return nil, err
				}
			}
		}
	}
	if pkt.Header.Reliable {
		if err = s.emit(p, []byte{game.Acknowledge}, false, now, &out); err != nil {
			return nil, err
		}
	}
	if err = s.advance(now, &out); err != nil {
		return nil, err
	}
	return out, nil
}

func (s *Session) advance(now time.Time, out *[]Delivery) error {
	if s.phase == WaitingRoster {
		// Native UpdateReliableManagement does not send an idle ACK with only
		// one received reliable packet. A separate roster-ACK gate deadlocks the
		// last joiner. Put the complete roster and loading command in one reliable
		// datagram instead: decoding order is preserved even if older rosters or
		// retries are lost/reordered, and the native client can begin loading.
		roster, err := s.rosterMessage()
		if err != nil {
			return err
		}
		s.phase = Loading
		s.deadline = now.Add(120 * time.Second)
		for _, p := range s.peers {
			v := s.loading
			v.Player = p.player.Slot
			b, _ := v.Message()
			body := append(append([]byte(nil), roster...), b...)
			if err := s.emit(p, body, true, now, out); err != nil {
				return err
			}
		}
	}
	if s.phase == Loading {
		for _, p := range s.peers {
			if !p.loaded {
				return nil
			}
		}
		s.phase = Loaded
		s.deadline = now.Add(15 * time.Second)
	}
	return nil
}

func (s *Session) Tick(now time.Time) ([]Delivery, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase == Closed || s.phase == Failed {
		return nil, ErrState
	}
	if s.phase == Finishing || s.phase == Finished {
		return s.tickEnding(now), nil
	}
	if s.phase != Playing && !now.Before(s.deadline) {
		s.phase = Failed
		return nil, ErrTimeout
	}
	var out []Delivery
	if err := s.advance(now, &out); err != nil {
		return nil, err
	}
	if err := s.synchronize(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceMovement(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceAdmissions(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceRound(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceMounts(now, &out); err != nil {
		return nil, err
	}
	if err := s.advancePickups(now, &out); err != nil {
		return nil, err
	}
	s.recordHitFrame(now)
	if err := s.advanceProjectiles(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceGrenades(now, &out); err != nil {
		return nil, err
	}
	if err := s.advanceObjectives(now, &out); err != nil {
		return nil, err
	}
	if s.phase == Playing && !s.endsAt.IsZero() && !now.Before(s.endsAt) {
		ending, err := s.finishOrOvertime(now)
		return append(out, ending...), err
	}
	for _, p := range s.peers {
		if p.departed || p.transportFailed {
			continue
		}
		if p.sceneReady && !p.life.dead {
			if actor := s.collider(p, now); actor != nil {
				actor.Advance(s.elapsed(now))
			}
		}
		for _, weapon := range p.weapons {
			weapon.ammo.Advance(now)
		}
		if p.joined {
			if body := p.ping.poll(now); body != nil {
				if err := s.emit(p, body, false, now, &out); err != nil {
					return nil, err
				}
			}
		}
		for _, b := range p.channel.retries(now) {
			out = append(out, Delivery{p.member.Endpoint, b})
		}
	}
	return out, nil
}
