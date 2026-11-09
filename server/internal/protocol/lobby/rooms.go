package lobby

import (
	"encoding/binary"
	"errors"
	"strconv"
	"strings"
)

const (
	CreateRoomRequest    uint32 = 135
	CreateRoomResponse   uint32 = 12
	ListRoomsRequest     uint32 = 128
	ListRoomsResponse    uint32 = 129
	FindRoomRequest      uint32 = 130
	FindRoomResponse     uint32 = 131
	FindRoomV1Request    uint32 = 97
	FindRoomV1Response   uint32 = 98
	FriendRoomsRequest   uint32 = 132
	FriendRoomsResponse  uint32 = 133
	KickRequest          uint32 = 24
	KickResponse         uint32 = 25
	PlayerCountRequest   uint32 = 101
	PlayerCountResponse  uint32 = 102
	RejoinRoomRequest    uint32 = 45
	RejoinRoomResponse   uint32 = 46
	JoinRoomRequest      uint32 = 15
	JoinRoomResponse     uint32 = 16
	LeaveRoomRequest     uint32 = 19
	LeaveRoomResponse    uint32 = 20
	MemberJoined         uint32 = 17
	MemberLeft           uint32 = 21
	MemberRemoved        uint32 = 32
	RoomInfoRequest      uint32 = 22
	RoomInfoResponse     uint32 = 23
	GameDataRequest      uint32 = 33
	GameDataResponse     uint32 = 34
	GameDataChanged      uint32 = 35
	PlayerDataRequest    uint32 = 36
	PlayerDataResponse   uint32 = 37
	PlayerDataChanged    uint32 = 38
	PlayerStatusRequest  uint32 = 39
	PlayerStatusResponse uint32 = 40
	PlayerStatusChanged  uint32 = 41
	LaunchRequest        uint32 = 42
	LaunchResponse       uint32 = 43
)

type Parameter struct {
	ID, Operator byte
	Value        string
}
type CreateRoom struct {
	Name                 string
	Type, Flags          byte
	Capacity             uint16
	GameData, PlayerData string
	Parameters           []Parameter
	Version              uint32
}
type ListRooms struct {
	// UserNumber is the legacy login's numeric "un" field (skin +0x14c),
	// not the application's GGI. Authentication comes from the lobby session.
	UserNumber, Offset uint32
	Limit              byte
	Parameters         []Parameter
	Type, Flags        byte
}
type JoinRoom struct {
	ID         uint32
	Status     byte
	PlayerData string
}

// All sizes are checked before slicing, including the original signed-short
// strings/blobs. No decoder accepts trailing bytes or embedded C-string NULs.
type roomReader struct {
	data   []byte
	offset int
	err    error
}

func (r *roomReader) take(n int) []byte {
	if r.err != nil || n < 0 || n > len(r.data)-r.offset {
		r.err = errors.New("truncated lobby room payload")
		return nil
	}
	b := r.data[r.offset : r.offset+n]
	r.offset += n
	return b
}
func (r *roomReader) byte() byte {
	b := r.take(1)
	if b == nil {
		return 0
	}
	return b[0]
}
func (r *roomReader) short() uint16 {
	b := r.take(2)
	if b == nil {
		return 0
	}
	return binary.BigEndian.Uint16(b)
}
func (r *roomReader) integer() uint32 {
	b := r.take(4)
	if b == nil {
		return 0
	}
	return binary.BigEndian.Uint32(b)
}
func (r *roomReader) text(max int) string {
	n := int(r.short())
	if n > max {
		r.err = errors.New("oversized lobby string")
		return ""
	}
	b := r.take(n)
	s := string(b)
	if strings.ContainsAny(s, "\x00\r\n") {
		r.err = errors.New("invalid lobby text")
	}
	return s
}
func (r *roomReader) parameters() []Parameter {
	n := int(r.byte())
	if n > 12 {
		r.err = errors.New("too many lobby parameters")
		return nil
	}
	p := make([]Parameter, 0, n)
	seen := map[byte]bool{}
	for i := 0; i < n; i++ {
		v := Parameter{ID: r.byte(), Operator: r.byte(), Value: r.text(128)}
		if v.ID > 11 || seen[v.ID] {
			r.err = errors.New("invalid lobby parameter")
		}
		seen[v.ID] = true
		p = append(p, v)
	}
	return p
}
func (r *roomReader) end() error {
	if r.err != nil {
		return r.err
	}
	if r.offset != len(r.data) {
		return errors.New("trailing lobby room payload")
	}
	return nil
}
func ParseCreateRoom(data []byte) (CreateRoom, error) {
	r := roomReader{data: data}
	v := CreateRoom{Name: r.text(16), Type: r.byte(), Flags: r.byte(), Capacity: r.short(), GameData: r.text(128), PlayerData: r.text(128)}
	v.Parameters = r.parameters()
	v.Version = r.integer()
	if v.Name == "" || v.Capacity < 1 || v.Capacity > 12 {
		r.err = errors.New("invalid room name or capacity")
	}
	return v, r.end()
}
func ParseListRooms(data []byte) (ListRooms, error) {
	r := roomReader{data: data}
	v := ListRooms{UserNumber: r.integer(), Limit: r.byte(), Offset: r.integer()}
	v.Parameters = r.parameters()
	v.Type = r.byte()
	v.Flags = r.byte()
	return v, r.end()
}
func ParseJoinRoom(data []byte) (JoinRoom, error) {
	r := roomReader{data: data}
	v := JoinRoom{ID: r.integer(), Status: r.byte(), PlayerData: r.text(128)}
	if v.ID == 0 {
		r.err = errors.New("invalid room ID")
	}
	return v, r.end()
}

// Version 2 name/friend searches have distinct headers, despite sharing the
// room-list response record. Friend names are counted BE16 strings, not a blob.
func ParseFindRoom(data []byte) (string, error) {
	r := roomReader{data: data}
	if r.integer() != 0 || r.byte() != 0 || r.integer() != 0 {
		r.err = errors.New("invalid name-search header")
	}
	name := r.text(128)
	if name == "" {
		r.err = errors.New("empty room search")
	}
	return name, r.end()
}

type FriendRooms struct {
	UserNumber  uint32
	Names       []string
	Parameters  []Parameter
	Type, Flags byte
}

func ParseFriendRooms(data []byte) (FriendRooms, error) {
	r := roomReader{data: data}
	v := FriendRooms{UserNumber: r.integer()}
	count, offset := r.byte(), r.integer()
	v.Type = r.byte()
	if n := r.byte(); n != count || n > 30 || offset != 0 {
		r.err = errors.New("invalid friend-search header")
	}
	if r.err == nil {
		for i := byte(0); i < count; i++ {
			name := r.text(128)
			if name == "" {
				r.err = errors.New("empty friend name")
			}
			v.Names = append(v.Names, name)
		}
	}
	v.Parameters = r.parameters()
	v.Flags = r.byte()
	return v, r.end()
}

// The native UpdateGameParam uses eleven signed integers and an unbounded
// sscanf string conversion into a 40-byte buffer. Admit the whole record before
// sharing it with another client; length checks alone do not protect that reader.
func ValidGameData(data string) bool {
	p := strings.Split(data, "|")
	if len(p) != 12 || len(p[11]) == 0 || len(p[11]) > 39 || strings.ContainsAny(p[11], " \t\r\n\x00") {
		return false
	}
	for _, v := range p[:11] {
		if _, err := strconv.ParseInt(v, 10, 32); err != nil {
			return false
		}
	}
	return true
}

func ParseRoomInfo(data []byte) (uint32, byte, error) {
	r := roomReader{data: data}
	id, mode := r.integer(), r.byte()
	return id, mode, r.end()
}
func ParsePlayerData(data []byte) (string, error) {
	r := roomReader{data: data}
	s := r.text(128)
	return s, r.end()
}
func ParseGameData(data []byte) (string, []Parameter, error) {
	r := roomReader{data: data}
	s := r.text(128)
	p := r.parameters()
	return s, p, r.end()
}

// GetLobbyInfo has its own original record order, not the list record layout.
func RoomInfo(r RoomRecord, players ...PlayerRecord) (Frame, error) {
	b := Integers(0)
	b = append(b, String(r.Name)...)
	b = append(b, String(r.GameData)...)
	b = append(b, r.Type, r.Flags)
	b = append(b, Integers(r.PlayerCount, r.Parameter2)...)
	b = append(b, String(r.Owner)...)
	b = append(b, r.Capacity, r.Started)
	b = append(b, Integers(r.Version)...)
	// The original callback always reads a trailing player count, even when
	// the request does not include the full roster.
	roster, err := JoinedRoom(players)
	if err != nil {
		return Frame{}, err
	}
	b = append(b, roster.Payload[4:]...)
	return Frame{Opcode: RoomInfoResponse, Payload: b}, nil
}

func Integers(values ...uint32) []byte {
	b := make([]byte, 4*len(values))
	for i, v := range values {
		binary.BigEndian.PutUint32(b[4*i:], v)
	}
	return b
}
func RoomStatus(opcode, status uint32) Frame { return Frame{Opcode: opcode, Payload: Integers(status)} }

type RoomRecord struct {
	ID                             uint32
	Name, Owner                    string
	Capacity, Started, Type, Flags byte
	PlayerCount, Parameter2        uint32
	GameData                       string
	Version                        uint32
}

// Record order is consumed by CLobbySkin::OnMPListSessionSuccess, version 2.
func RoomList(records []RoomRecord) (Frame, error) {
	return RoomListVersion(records, ListRoomsResponse, 2)
}

func RoomListVersion(records []RoomRecord, opcode uint32, version int) (Frame, error) {
	b := Integers(0, uint32(len(records)))
	for _, r := range records {
		if len(r.Name) > 16 || len(r.Owner) > 16 || len(r.GameData) > 512 {
			return Frame{}, errors.New("oversized room record")
		}
		b = append(b, Integers(r.ID)...)
		b = append(b, String(r.Name)...)
		b = append(b, String(r.Owner)...)
		b = append(b, r.Capacity, r.Started, r.Type, r.Flags)
		b = append(b, Integers(r.PlayerCount, r.Parameter2)...)
		b = append(b, String(r.GameData)...)
		if version >= 2 {
			b = append(b, Integers(r.Version)...)
		}
	}
	if len(b)+6 > MaxFrame {
		return Frame{}, errors.New("room list exceeds native frame")
	}
	return Frame{Opcode: opcode, Payload: b}, nil
}

type PlayerRecord struct {
	ID, Name string
	Status   byte
	Data     string
}

// RejoinedRoom restores an existing lobby membership after a completed match.
// A waiting room carries an empty game endpoint and zero ports; it must not
// advertise a running scene without issuing an authenticated transport grant.
func RejoinedRoom(r RoomRecord, players []PlayerRecord) (Frame, error) {
	if r.Started != 0 || len(r.Name) == 0 || len(r.Name) > 16 || len(r.GameData) > 128 ||
		r.Capacity < 1 || r.Capacity > 12 || len(players) > int(r.Capacity) {
		return Frame{}, errors.New("invalid rejoin room")
	}
	roster, err := JoinedRoom(players)
	if err != nil {
		return Frame{}, err
	}
	b := append(Integers(0), String(r.Name)...)
	b = append(b, r.Type, r.Flags, r.Capacity)
	b = append(b, String(r.GameData)...)
	b = append(b, 0) // Not started.
	b = append(b, String("")...)
	b = append(b, 0, 0, 0, 0) // Both native endpoint ports.
	b = append(b, roster.Payload[4:]...)
	if len(b)+6 > MaxFrame {
		return Frame{}, errors.New("rejoin exceeds native frame")
	}
	return Frame{Opcode: RejoinRoomResponse, Payload: b}, nil
}

func PlayerBytes(p PlayerRecord) ([]byte, error) {
	if len(p.ID) == 0 || len(p.ID) > 16 || len(p.Name) == 0 || len(p.Name) > 16 || len(p.Data) > 512 {
		return nil, errors.New("invalid lobby player record")
	}
	b := append(String(p.ID), String(p.Name)...)
	b = append(b, p.Status)
	return append(b, String(p.Data)...), nil
}
func JoinedRoom(players []PlayerRecord) (Frame, error) {
	b := Integers(0, uint32(len(players)))
	for _, p := range players {
		v, err := PlayerBytes(p)
		if err != nil {
			return Frame{}, err
		}
		b = append(b, v...)
	}
	if len(b)+6 > MaxFrame {
		return Frame{}, errors.New("player list exceeds native frame")
	}
	return Frame{Opcode: JoinRoomResponse, Payload: b}, nil
}
