package native

import (
	"fmt"
	"strings"
	"unicode/utf8"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

func informationLabel(value string) string {
	if len(value) <= 15 {
		return value
	}
	value = value[:15]
	for !utf8.ValidString(value) {
		value = value[:len(value)-1]
	}
	return value
}

func (s *Services) userInformation(r legacy.Request) (string, error) {
	owner := r.Fields["u"]
	if r.Function == 51 || r.Function == 54 {
		if r.Fields["n"] != "" || r.Fields["un"] != "" {
			return "", fmt.Errorf("profile owner required")
		}
		_, name, err := accounts.PublicIdentity(s.Store, owner)
		if err != nil {
			return "", err
		}
		account, err := accounts.Authenticate(s.Store, name, r.Fields["p"])
		if err != nil || account != owner {
			return "", fmt.Errorf("password proof required")
		}
		key, field := "email", "ne"
		if r.Function == 54 {
			key, field = "country", "nc"
		}
		value, exists := r.Fields[field]
		if !exists {
			return "", fmt.Errorf("missing profile field")
		}
		return "", accounts.PatchInformation(s.Store, owner, map[string]string{key: value})
	}
	if r.Function == 82 {
		// The original editor has no target selector. Reject one explicitly,
		// rather than silently updating a different identity than the caller.
		if r.Fields["n"] != "" || r.Fields["un"] != "" {
			return "", fmt.Errorf("profile owner required")
		}
		return "", accounts.PatchInformation(s.Store, owner, r.Fields)
	}
	account := owner
	if r.Fields["n"] != "" || r.Fields["un"] != "" {
		var err error
		account, err = accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
		if err != nil {
			return "", err
		}
	}
	identity, name, err := accounts.LobbyIdentity(s.Store, account)
	if err != nil {
		return "", err
	}
	if r.Function == 58 {
		return identity, nil
	}
	info, err := accounts.ReadInformation(s.Store, account)
	if err != nil {
		return "", err
	}
	if r.Function == 53 {
		if _, err := nativeInteger(r.Fields, "nf", 0, 0, 1); err != nil {
			return "", err
		}
		return "c|" + info.Country + "|", nil
	}
	if account != owner {
		// Email visibility's historical meaning is unverified. Only the
		// explicitly public country/name/identity are exposed to other users.
		info = accounts.Information{Country: info.Country}
	}
	name = informationLabel(name)
	fields := []string{"u", identity, "username", name, "email", info.Email,
		"email_flag", fmt.Sprint(info.EmailFlag), "country", info.Country,
		"reserved_1", "", "reserved_2", "", "reserved_3", "0",
		"firstname", info.FirstName, "lastname", info.LastName, "sex", fmt.Sprint(info.Sex),
		"birthday", info.Birthday, "online_time", "0", "trophy", "0"}
	if account != owner {
		fields = append([]string{"n", name}, fields...)
	}
	return strings.Join(fields, "|") + "|", nil
}
