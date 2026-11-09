package native

import (
	"fmt"
	"strconv"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

// The native state reader accepts n/name, t/presence, current game and
// a/application ID. Public lookup never returns credentials or private data.
func (s *Services) userPresence(r legacy.Request) (string, error) {
	account, err := accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
	if err != nil {
		return "", err
	}
	number, name, err := accounts.PublicIdentity(s.Store, account)
	if err != nil {
		return "", err
	}
	key, identity := "n", name
	// Both original reader variants allocate 16 bytes for the public selector.
	// Preserve long identities by number, never truncate into another name.
	if len(name) > 15 {
		key, identity = "un", strconv.FormatInt(number, 10)
	}
	return fmt.Sprintf("%s|%s|t|%d|g|28003|a|28003|", key, identity, s.Sessions.OnlineState(account, time.Now())), nil
}
