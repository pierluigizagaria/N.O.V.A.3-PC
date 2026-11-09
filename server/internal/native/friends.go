package native

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

func nativeInteger(fields map[string]string, key string, fallback, minimum, maximum int) (int, error) {
	text, exists := fields[key]
	if !exists {
		return fallback, nil
	}
	v, err := strconv.Atoi(text)
	if err != nil || v < minimum || v > maximum {
		return 0, fmt.Errorf("invalid numeric field %s", key)
	}
	return v, nil
}

func (s *Services) friends(r legacy.Request) (string, error) {
	owner := r.Fields["u"]
	switch r.Function {
	case 59, 60, 62:
		target, err := accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
		if err != nil {
			return "", err
		}
		if r.Function == 60 {
			return "", accounts.DeleteContact(s.Store, owner, target)
		}
		if r.Function == 62 {
			accept, err := nativeInteger(r.Fields, "c", -1, 0, 1)
			if err != nil || accept < 0 {
				return "", fmt.Errorf("confirmation required")
			}
			return "", accounts.ConfirmContact(s.Store, owner, target, accept == 1)
		}
		return "", accounts.AddContact(s.Store, owner, target)
	case 61, 73:
		start, err := nativeInteger(r.Fields, "s", 0, 0, accounts.MaxContacts)
		if err != nil {
			return "", err
		}
		count, err := nativeInteger(r.Fields, "c", 0, 0, accounts.MaxContacts)
		if err != nil {
			return "", err
		}
		if _, err := nativeInteger(r.Fields, "hp", 0, 0, 1); err != nil {
			return "", err
		}
		if r.Function == 73 {
			start, count = 0, 0
		}
		entries, err := accounts.Contacts(s.Store, owner, start, count)
		if err != nil {
			return "", err
		}
		if r.Function == 73 {
			return strconv.Itoa(len(entries)) + "|", nil
		}
		// processFriendsList skips fields 0 and 2; record fields are caret
		// separated and bounded below the original 512-byte scratch buffer.
		var body strings.Builder
		fmt.Fprintf(&body, "c|%d|f|", len(entries))
		for _, c := range entries {
			// The list UI copies display names into a 50-byte stack buffer;
			// reuse the lobby's bounded label while retaining the full public ID.
			if len(c.Name) > 16 {
				c.Name = c.Name[:16]
			}
			state := s.Sessions.OnlineState(c.Account, time.Now())
			key, _, err := s.portrait(c.Account)
			if err != nil {
				return "", err
			}
			fmt.Fprintf(&body, "%d^%s^%s^0,0,0^0^%d^28003^0^en^%s^0^1|", c.Number, c.Name, c.Country, state, key)
		}
		return body.String(), nil
	}
	return "", fmt.Errorf("unsupported social operation")
}
