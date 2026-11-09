package game

import (
	_ "embed"
	"encoding/binary"
	"encoding/json"
)

const PresetChat byte = 130

//go:embed chat_ids.json
var chatData []byte

var globalChat = func() map[uint32]bool {
	var data struct {
		Version  uint32   `json:"schema_version"`
		Messages []uint32 `json:"global_messages"`
	}
	if json.Unmarshal(chatData, &data) != nil || data.Version != 1 || len(data.Messages) == 0 || len(data.Messages) > 64 {
		panic("invalid embedded chat metadata")
	}
	result := make(map[uint32]bool, len(data.Messages))
	for _, id := range data.Messages {
		if id > 65535 || result[id] {
			panic("invalid embedded chat identity")
		}
		result[id] = true
	}
	return result
}()

var teamChat = func() map[byte]map[uint32]bool {
	var data struct {
		Teams map[byte][]uint32 `json:"team_messages"`
	}
	if json.Unmarshal(chatData, &data) != nil {
		panic("invalid team chat metadata")
	}
	result := make(map[byte]map[uint32]bool)
	for mode, ids := range data.Teams {
		if len(ids) > 64 {
			panic("oversized team chat metadata")
		}
		result[mode] = make(map[uint32]bool)
		for _, id := range ids {
			if id > 65535 || result[mode][id] {
				panic("invalid team chat identity")
			}
			result[mode][id] = true
		}
	}
	return result
}()

// Chat carries a localized preset ID, never arbitrary text. Team zero means
// everybody. The match owner authenticates Actor and selects recipients.
type Chat struct {
	Team, Actor byte
	MessageID   uint32
}

func (c Chat) Message() ([]byte, error) {
	b := []byte{PresetChat, c.Team, c.Actor, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(b[3:], c.MessageID)
	if _, err := ReadChat(b); err != nil {
		return nil, err
	}
	return b, nil
}

func ReadChat(b []byte) (Chat, error) {
	if len(b) != 7 || b[0] != PresetChat || b[1] > 2 || b[2] >= 12 {
		return Chat{}, ErrPacket
	}
	return Chat{b[1], b[2], binary.BigEndian.Uint32(b[3:])}, nil
}

func (c Chat) GlobalAllowed() bool { return c.Team == 0 && globalChat[c.MessageID] }

func (c Chat) Allowed(mode, team byte) bool {
	return c.GlobalAllowed() || (c.Team != 0 && c.Team == team && teamChat[mode][c.MessageID])
}
