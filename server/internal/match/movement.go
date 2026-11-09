package match

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"net/netip"
	"nova3.test/backend/internal/collision"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/rooms"
	"nova3.test/backend/internal/world"
)

// movement is the owner-approved connection/movement preview. The server owns
// admission, spawn identities, generations, clocks and publication. Positions
// are authenticated client proposals: physics/combat authority is NOT complete.
// Keep this limitation explicit until the simulation replaces this boundary.
type movement struct {
	bounds           world.PositionBounds
	scene            *world.Scene
	spawns           []world.SpawnPoint
	spawnPool        []world.SpawnPoint
	poses            []pc.Snapshot
	upstream         []uint32
	seen             []bool
	seed             uint32
	publication      cadence
	primaryWeapons   map[byte]uint16
	secondaryWeapons map[byte]uint16
	addons           map[byte]byte
	items            map[byte]uint16
	loadouts         map[string]LoadoutPolicy
	zones            map[int32]bool
}

// NewMovement prepares the selected native mode and real map before handoff.
func NewMovement(r *rooms.Reservation, options Options, resolve ResolveCredential, now time.Time) (*Bound, error) {
	if !SupportedMode(options.Loading.Mode) || options.Loading.Parameter1 > 2147483647/60000 || options.Loading.Parameter2 > 2147483647 {
		return nil, ErrState
	}
	m, err := world.LoadMap(options.Loading.Map)
	if err != nil || !m.Supports(options.Loading.Mode) {
		return nil, ErrState
	}
	b, err := NewBound(r, options, resolve, now)
	if err != nil {
		return nil, err
	}
	var seed [4]byte
	if _, err = rand.Read(seed[:]); err != nil {
		b.Close()
		return nil, err
	}
	state := &movement{seed: binary.LittleEndian.Uint32(seed[:])}
	state.bounds = m.PositionBounds
	state.scene, err = m.NewScene()
	if err != nil {
		b.Close()
		return nil, err
	}
	if options.Loadouts != nil {
		state.loadouts = make(map[string]LoadoutPolicy, len(b.session.peers))
		for _, p := range b.session.peers {
			policy, ok := options.Loadouts[p.member.Account]
			if !ok {
				b.Close()
				return nil, ErrAuthorization
			}
			state.loadouts[p.member.Account] = policy
		}
	}
	state.zones = make(map[int32]bool, len(m.Zones))
	for _, id := range m.Zones {
		state.zones[id] = true
	}
	state.primaryWeapons = make(map[byte]uint16, len(options.PrimaryWeapons))
	for ordinal, id := range options.PrimaryWeapons {
		state.primaryWeapons[ordinal] = id
	}
	state.secondaryWeapons = make(map[byte]uint16, len(options.SecondaryWeapons))
	for ordinal, id := range options.SecondaryWeapons {
		state.secondaryWeapons[ordinal] = id
	}
	state.addons = make(map[byte]byte, len(options.Addons))
	state.items = make(map[byte]uint16, len(options.Items))
	for selection, id := range options.Items {
		state.items[selection] = id
	}
	for selection, id := range options.Addons {
		state.addons[selection] = id
	}
	state.spawnPool = append([]world.SpawnPoint(nil), m.Spawns...)
	n := int(options.Loading.Capacity)
	if len(state.spawnPool) == 0 {
		b.Close()
		return nil, ErrState
	}
	// These slots are storage only. The native Start grants select each actual
	// spawn with current teams and live occupants after synchronization.
	state.spawns = make([]world.SpawnPoint, n)
	for i := range state.spawns {
		state.spawns[i] = state.spawnPool[i%len(state.spawnPool)]
	}
	state.poses = make([]pc.Snapshot, n)
	state.upstream, state.seen = make([]uint32, n), make([]bool, n)
	for i, spawn := range state.spawns {
		position := spawn.Transform.Position
		yaw, _ := pc.EncodeAngle(spawn.Transform.Rotation.Z)
		state.poses[i] = pc.Snapshot{Slot: byte(i), Generation: 1, Position: [3]float32{float32(position.X), float32(position.Y), float32(position.Z)}, Yaw: yaw}
	}
	b.session.movement = state
	if err := b.session.initObjectives(); err != nil {
		b.Close()
		return nil, err
	}
	if err := b.session.initPickups(); err != nil {
		return nil, err
	}
	if options.Authority != nil {
		for _, definition := range options.Authority.Mounts(options.Loading.Map) {
			if definition.Modes >= 0 && definition.Modes&modeMapBit(options.Loading.Mode) == 0 {
				continue
			}
			mount := &mountState{definition: definition, position: world.Vec3{X: float64(definition.Position[0]), Y: float64(definition.Position[1]), Z: float64(definition.Position[2])}, rotation: [3]float64{float64(definition.Rotation[0]), float64(definition.Rotation[1]), float64(definition.Rotation[2])}, health: definition.Health}
			b.session.resetMech(mount, now)
			b.session.mounts = append(b.session.mounts, mount)
		}
	}
	return b, nil
}

func (s *Session) advanceMovement(now time.Time, out *[]Delivery) error {
	if s.movement == nil {
		return nil
	}
	if s.phase == Accepting {
		for _, p := range s.peers {
			if !p.joined {
				return nil
			}
		}
		s.phase = WaitingRoster
	}
	if s.phase == Synchronized {
		for i, p := range s.peers {
			if err := s.prepareSpawn(p); err != nil {
				return err
			}
			p.lastReceived = now
			start := game.Start{RemainingMS: 2000, ElapsedMS: s.elapsed(now), Seed: s.movement.seed, Spawn: s.movement.spawns[i].ID}
			if err := s.emit(p, start.Message(), true, now, out); err != nil {
				return err
			}
		}
		s.phase = Playing
		// Round admission/countdown starts separately when at least two scenes
		// are ready. Loading and the connection barrier consume no match time.
		s.deadline = time.Time{}
		s.movement.publication = newCadence(now)
	}
	return nil
}

func (s *Session) propose(from netip.AddrPort, records []pc.Snapshot, now time.Time) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	p := s.byEndpoint[from]
	if (s.phase != Playing && s.phase != Finishing && s.phase != Finished) || s.movement == nil || p == nil || p.transportFailed || len(records) != 1 {
		return ErrState
	}
	v := records[0]
	i := int(p.player.Slot)
	state := s.movement
	if v.Valid() && int(v.Slot) == i && pc.Newer(state.poses[i].Generation, v.Generation) {
		return nil // An in-flight pose from the previous life is not a new input.
	}
	if !v.Valid() || int(v.Slot) != i || v.Generation != state.poses[i].Generation {
		return fmt.Errorf("%w: pose slot=%d claimed=%d life=%d expected=%d", ErrPeer, i, v.Slot, v.Generation, state.poses[i].Generation)
	}
	if state.seen[i] && !pc.Newer(v.Sequence, state.upstream[i]) {
		return nil // Reordered/lost UDP samples cannot roll movement backwards.
	}
	// Validate the source clock before preserving it for visual interpolation.
	// Outgoing sequence and life identity remain server-owned.
	delta := int32(v.Time - s.elapsed(now))
	if delta < -2000 || delta > 2000 {
		return fmt.Errorf("%w: pose clock delta=%d slot=%d", ErrPeer, delta, i)
	}
	if state.seen[i] && !pc.Newer(v.Time, p.lastPositionTime) && !p.life.dead && !p.roundSpawnPending {
		return nil // An older presentation clock is not a fresh position proposal.
	}
	state.upstream[i], state.seen[i] = v.Sequence, true
	p.lastPositionTime = v.Time
	if p.mount != nil && p.mount.vehicle.definition.Category != 3 {
		// An on-foot datagram in flight cannot move the occupied mount back to
		// its previous human position. Mount state has its own native timeline.
		p.lastReceived = now
		return nil
	}
	if s.phase == Playing && !p.life.dead && !p.life.frozen && !p.roundSpawnPending {
		p.positionInputs++
		if state.poses[i].Position != v.Position || state.poses[i].Yaw != v.Yaw || state.poses[i].Pitch != v.Pitch {
			p.positionChanges++
		}
		state.poses[i].Position, state.poses[i].Yaw, state.poses[i].Pitch = v.Position, v.Yaw, v.Pitch
		// Keep the accepted sample's actual clock. Re-stamping a held pose at
		// every publication turns steady walking into stop/double-speed steps
		// when the two30Hz schedulers cross; remote animation sees that too.
		state.poses[i].Time = v.Time
		if actor := s.collider(p, now); actor != nil {
			actor.Observe(collision.Vector(v.Position), s.elapsed(now))
		}
	}
	p.lastReceived = now
	return nil
}

func (s *Session) publication(now time.Time) ([]byte, []byte, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase != Playing || s.movement == nil || !s.movement.publication.due(now) {
		return nil, nil, nil
	}
	lives := make([]pc.Life, 0, len(s.peers))
	records := make([]pc.Snapshot, 0, len(s.peers))
	for i, p := range s.peers {
		if p.departed || (p.admitting != "" && !p.sceneReady) {
			continue
		}
		pose := &s.movement.poses[i]
		pose.Sequence++
		if pose.Time == 0 || p.life.dead || p.roundSpawnPending {
			pose.Time = s.elapsed(now)
		}
		lives = append(lives, pc.Life{Slot: pose.Slot, Generation: pose.Generation})
		records = append(records, *pose)
	}
	life, err := pc.EncodeLives(lives)
	if err != nil {
		return nil, nil, err
	}
	poses, err := pc.EncodeSnapshots(records)
	if err == nil {
		s.snapshotBatches++
	}
	return life, poses, err
}
