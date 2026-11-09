//go:build debug

// nova-server composes trusted administration and explicit local service commands.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/application"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/loadout"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/transport/loopback"
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
	directory := flags.String("directory", "", "explicit dedicated server data root; otherwise discover the repository")
	listen := flags.String("listen", "127.0.0.1:8085", "loopback shop bind")
	nativeHTTP := flags.String("native-http", "127.1.1.1:80", "native legacy/configuration HTTP bind")
	nativeEve := flags.String("native-eve", "127.1.1.1:20001", "native Eve HTTP bind")
	nativeTLS := flags.String("native-tls", "127.1.1.1:18443", "native authorization/storage TLS bind")
	nativeLegacyTLS := flags.String("native-legacy-tls", "127.1.1.1:443", "original legacy configuration TLS bind")
	nativeLobby := flags.String("native-lobby", "127.1.1.1:15200", "native lobby TCP bind")
	compatibility := flags.String("compatibility-http", "127.0.0.1:8085", "host price bridge HTTP bind")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if !catalogue.ValidID(*state) {
		return errors.New("invalid state directory name")
	}
	args = flags.Args()
	if len(args) == 0 {
		return errors.New("commands: catalogue-admit FILE | account-create NAME | provision ID VERSION | reset ID QUANTITY | equip ID FILE | show ID | session-issue ID | session-resolve | session-revoke | serve VERSION MAPPING")
	}
	// Reject argument mistakes before creating a database.
	sizes := map[string]int{"catalogue-admit": 2, "account-create": 2, "provision": 3, "reset": 3, "equip": 3, "show": 2, "session-issue": 2, "session-resolve": 1, "session-revoke": 1}
	sizes["serve"] = 3
	sizes["serve-native"] = 3
	sizes["password-set"] = 2
	sizes["tls-init"] = 1
	if sizes[args[0]] != len(args) {
		return errors.New("unknown command or wrong argument count")
	}
	root := *directory
	var err error
	if root == "" {
		root, err = workspace()
	} else {
		root, err = filepath.Abs(root)
	}
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
	case "tls-init":
		_, err = loopback.Certificate(filepath.Join(root, "local", "server", *state))
		result = map[string]string{"certificate": filepath.Join(root, "local", "server", *state, "localhost.pem")}
	case "serve", "serve-native":
		c, e := persistence.Catalogue(s.DB, args[1])
		if e != nil {
			return e
		}
		data, e := read(args[2])
		if e != nil {
			return e
		}
		m, e := shop.DecodeMapping(data)
		if e != nil {
			return e
		}
		adapter, e := shop.New(c, m)
		if e != nil {
			return e
		}
		if args[0] == "serve-native" {
			ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
			defer stop()
			return application.RunNative(ctx, s, adapter, application.NativeServerOptions{HTTP: *nativeHTTP, Eve: *nativeEve, TLS: *nativeTLS, LegacyTLS: *nativeLegacyTLS, Lobby: *nativeLobby, Compatibility: *compatibility, Directory: filepath.Join(root, "local", "server", *state), Ready: os.Stdout, Diagnostics: os.Stderr})
		}
		listener, e := shop.ListenLocal(*listen)
		if e != nil {
			return e
		}
		defer listener.Close()
		server := &http.Server{Handler: shop.Handler(s, adapter), ReadHeaderTimeout: 3 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 5 * time.Second, IdleTimeout: 15 * time.Second, MaxHeaderBytes: 8192}
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		go func() {
			<-ctx.Done()
			shutdown, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			server.Shutdown(shutdown)
		}()
		json.NewEncoder(os.Stdout).Encode(map[string]string{"listen": "http://" + listener.Addr().String(), "version": c.Version})
		e = server.Serve(listener)
		if errors.Is(e, http.ErrServerClosed) {
			return nil
		}
		return e
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
	case "password-set":
		var password []byte
		password, err = io.ReadAll(io.LimitReader(os.Stdin, 66))
		if err == nil {
			err = accounts.SetPassword(s, args[1], strings.TrimSuffix(strings.TrimSuffix(string(password), "\n"), "\r"))
		}
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

func runUserCommand(args []string) error {
	err := run(args)
	if errors.Is(err, flag.ErrHelp) {
		return nil
	}
	return err
}
