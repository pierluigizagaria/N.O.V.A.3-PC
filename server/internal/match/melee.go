package match

import (
	"math"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/world"
	"time"
)

func (s *Session) melee(p *peer, message []byte, now time.Time, out *[]Delivery) error {
	if s.authority == nil || s.movement == nil || p.life.dead || p.life.frozen || p.mount != nil || p.roundSpawnPending || s.round.runningSince.IsZero() || now.Before(p.nextMelee) {
		return nil
	}
	v, _ := game.ReadMeleeRequest(message)
	if int(v.Victim) >= len(s.peers) {
		return nil
	}
	target := s.peers[v.Victim]
	if !s.canDamage(p, target) {
		return nil
	}
	// The Wi-Fi host reads the trailing weapon byte but MeleeDamageMp does
	// not use it to authorize the hit. The native caller can encode255 here;
	// melee belongs to power7, not to the equipped firearm's wire identity.
	age := int32(s.elapsed(now) - v.Time)
	if age < -2000 || age > 2000 || int32(v.Time-p.life.bornAt) < 0 {
		return nil
	}
	tuning := s.authority.Melee()
	if tuning.TargetRange <= 0 || tuning.Damage <= 0 {
		return nil
	}
	if len(s.hitHistory) == 0 {
		s.recordHitFrame(now)
	}
	stamp := s.confirmationTime(p, v.Time, now)
	attackerPose, ok := s.historicalPlayer(p, stamp)
	if !ok || attackerPose.mountCategory != 0 {
		return nil
	}
	targetPose, ok := s.displayedPlayer(target, stamp, stamp-pc.PresentationDelayMS)
	if !ok || targetPose.mountCategory != 0 && targetPose.mountCategory != 3 {
		return nil
	}
	// Native CheckMelee uses actor roots and its component42 target range,
	// then a full forward cone. Power7's radius and capsule centers are not
	// that selection rule. The dedicated server validates the claimed victim
	// against the accepted historical roots, never a supplied damage report.
	delta := targetPose.position.Sub(attackerPose.position)
	distance := math.Sqrt(delta.Dot(delta))
	if distance > float64(tuning.TargetRange) || distance == 0 {
		return nil
	}
	yaw := pc.DecodeAngle(attackerPose.yaw) * math.Pi / 180
	pitch := pc.DecodeAngle(attackerPose.pitch) * math.Pi / 180
	facing := world.Vec3{X: -math.Sin(yaw) * math.Cos(pitch), Y: math.Cos(yaw) * math.Cos(pitch), Z: math.Sin(pitch)}
	if delta.Mul(1/distance).Dot(facing) < float64(tuning.MinimumForwardDot) {
		return nil
	}
	height := float64(s.authority.Player().Body.Height * tuning.SightHeightFraction)
	from := attackerPose.position.Add(world.Vec3{Z: height})
	to := targetPose.position.Add(world.Vec3{Z: height})
	if _, blocked, err := s.movement.scene.Raycast(from, to, world.Filter{Group: 1, Mask: 0xf36f}, nil); blocked || err != nil {
		return err
	}
	p.nextMelee = now.Add(time.Duration(tuning.CooldownMS) * time.Millisecond)
	return s.damageKind(p, target, p.weapon, tuning.Damage, 0, 5, 7, now, out)
}
