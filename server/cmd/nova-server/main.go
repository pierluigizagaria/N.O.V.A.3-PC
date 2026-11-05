// nova-server is an offline administrative interface, not a game protocol listener.
package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/loadout"
	"nova3.test/backend/internal/persistence"
)

func workspace() (string, error) {
	path, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for {
		if _, err = os.Stat(filepath.Join(path, "server", "go.mod")); err == nil {
			if _, err = os.Stat(filepath.Join(path, "CMakeLists.txt")); err == nil {
				return path, nil
			}
		}
		parent := filepath.Dir(path)
		if parent == path {
			return "", errors.New("run from inside the repository")
		}
		path = parent
	}
}
func read(path string) ([]byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	data, err := io.ReadAll(io.LimitReader(file, 1024*1024+1))
	if err != nil {
		return nil, err
	}
	if len(data) > 1024*1024 {
		return nil, errors.New("input exceeds 1 MiB")
	}
	return data, nil
}
func run(args []string) error {
	flags := flag.NewFlagSet("nova-server", flag.ContinueOnError)
	state := flags.String("state", "default", "state directory name under local/server")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if !catalogue.ValidID(*state) {
		return errors.New("invalid state directory name")
	}
	args = flags.Args()
	if len(args) == 0 {
		return errors.New("commands: catalogue-admit FILE | account-create NAME | provision ID VERSION | reset ID QUANTITY | equip ID FILE | show ID | session-issue ID | session-resolve | session-revoke")
	}
	// Reject argument mistakes before creating a database.
	sizes := map[string]int{"catalogue-admit": 2, "account-create": 2, "provision": 3, "reset": 3, "equip": 3, "show": 2, "session-issue": 2, "session-resolve": 1, "session-revoke": 1}
	if sizes[args[0]] != len(args) {
		return errors.New("unknown command or wrong argument count")
	}
	root, err := workspace()
	if err != nil {
		return err
	}
	s, err := persistence.Open(filepath.Join(root, "local", "server", *state, "backend.db"))
	if err != nil {
		return err
	}
	defer s.Close()
	var result any = map[string]bool{"ok": true}
	switch args[0] {
	case "catalogue-admit":
		data, e := read(args[1])
		if e != nil {
			return e
		}
		c, e := catalogue.Decode(data)
		if e != nil {
			return e
		}
		if err = s.Admit(c); err != nil {
			return err
		}
		result = map[string]any{"version": c.Version, "source": c.Source, "items": len(c.Items)}
	case "account-create":
		result, err = accounts.Create(s, args[1])
	case "provision":
		err = grants.Provision(s, args[1], args[2])
	case "reset":
		var quantity int
		quantity, err = strconv.Atoi(args[2])
		if err == nil {
			err = grants.ResetConsumables(s, args[1], quantity)
		}
	case "equip":
		var data []byte
		data, err = read(args[2])
		if err != nil {
			return err
		}
		var selection loadout.Selection
		if err = json.Unmarshal(data, &selection); err == nil {
			err = loadout.Set(s, args[1], selection)
		}
	case "show":
		var inventory []grants.Entitlement
		var selection loadout.Selection
		inventory, err = grants.Inventory(s, args[1])
		if err == nil {
			selection, err = loadout.Get(s, args[1])
		}
		result = map[string]any{"account": args[1], "inventory": inventory, "loadout": selection}
	case "session-issue":
		var token string
		token, err = accounts.Issue(s, args[1], time.Hour)
		result = map[string]string{"token": token}
	case "session-resolve", "session-revoke":
		var token []byte
		token, err = io.ReadAll(io.LimitReader(os.Stdin, 66))
		if err != nil {
			return err
		}
		value := strings.TrimSpace(string(token))
		if args[0] == "session-resolve" {
			var id string
			id, err = accounts.Resolve(s, value)
			result = map[string]string{"account": id}
		} else {
			err = accounts.Revoke(s, value)
		}
	}
	if err != nil {
		return err
	}
	return json.NewEncoder(os.Stdout).Encode(result)
}
func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
