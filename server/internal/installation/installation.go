// Package installation owns executable-relative server data, never client files.
package installation

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/application"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/world"
)

type Installation struct {
	Directory string
	Shop      *shop.Adapter
	Authority *authority.Definitions
}

// Load validates existing data without creating a database or applying migrations.
func Load(executable string) (*Installation, error) {
	executable, err := filepath.Abs(executable)
	if err != nil {
		return nil, err
	}
	directory := filepath.Join(filepath.Dir(executable), "data")
	for _, name := range []string{"backend.db", "mapping.json", "authority.json"} {
		info, e := os.Lstat(filepath.Join(directory, name))
		if e != nil || !info.Mode().IsRegular() {
			return nil, fmt.Errorf("installed server data missing or not a regular file: data/%s", name)
		}
	}
	f, err := os.Open(filepath.Join(directory, "mapping.json"))
	if err != nil {
		return nil, err
	}
	data, err := io.ReadAll(io.LimitReader(f, (1<<20)+1))
	f.Close()
	if err != nil {
		return nil, err
	}
	mapping, err := shop.DecodeMapping(data)
	if err != nil {
		return nil, fmt.Errorf("installed mapping: %w", err)
	}
	database := filepath.ToSlash(filepath.Join(directory, "backend.db"))
	if !strings.HasPrefix(database, "/") {
		database = "/" + database
	}
	uri := url.URL{Scheme: "file", Path: database, RawQuery: "mode=ro"}
	db, err := sql.Open("sqlite", uri.String())
	if err != nil {
		return nil, err
	}
	defer db.Close()
	rows, err := db.Query("SELECT version,body FROM catalogues")
	if err != nil {
		return nil, fmt.Errorf("installed catalogue unavailable: %w", err)
	}
	defer rows.Close()
	var selected *shop.Adapter
	for rows.Next() {
		var version, body string
		if err = rows.Scan(&version, &body); err != nil {
			return nil, err
		}
		c, e := catalogue.Decode([]byte(body))
		if e != nil {
			return nil, fmt.Errorf("invalid installed catalogue: %w", e)
		}
		if c.Version != version {
			return nil, errors.New("installed catalogue identity mismatch")
		}
		if c.LibrarySHA256 != mapping.Library || c.ResourceSHA256 != mapping.Resource {
			continue
		}
		adapter, e := shop.New(c, mapping)
		if e != nil {
			return nil, fmt.Errorf("installed mapping does not match catalogue: %w", e)
		}
		if selected != nil {
			return nil, errors.New("installed data contains multiple compatible catalogues")
		}
		selected = adapter
	}
	if err = rows.Err(); err != nil {
		return nil, err
	}
	if selected == nil {
		return nil, errors.New("installed data has no catalogue compatible with mapping.json")
	}
	f, err = os.Open(filepath.Join(directory, "authority.json"))
	if err != nil {
		return nil, err
	}
	data, err = io.ReadAll(io.LimitReader(f, authority.MaxBytes+1))
	f.Close()
	if err != nil {
		return nil, err
	}
	definitions, err := authority.Decode(data)
	if err != nil {
		return nil, fmt.Errorf("installed authority: %w", err)
	}
	if definitions.LibrarySHA256() != mapping.Library {
		return nil, errors.New("authority library does not match catalogue")
	}
	for _, group := range []string{"primary", "secondary"} {
		for _, item := range selected.Groups[group] {
			if _, ok := definitions.Weapon(int32(item.Native.TypeID)); !ok {
				return nil, fmt.Errorf("authority has no definition for catalogue weapon %s", item.ID)
			}
		}
	}
	if err := world.CheckMaps(); err != nil {
		return nil, fmt.Errorf("embedded technical maps: %w", err)
	}
	if err := match.CheckModeData(); err != nil {
		return nil, fmt.Errorf("multiplayer objective data: %w", err)
	}
	return &Installation{Directory: directory, Shop: selected, Authority: definitions}, nil
}

func (i *Installation) Run(ctx context.Context, output, diagnostics io.Writer) error {
	return i.RunDomain(ctx, "", output, diagnostics)
}

func (i *Installation) RunDomain(ctx context.Context, domain string, output, diagnostics io.Writer) error {
	return i.run(ctx, domain, "", false, output, diagnostics)
}

func (i *Installation) RunAddress(ctx context.Context, address string, output, diagnostics io.Writer) error {
	return i.run(ctx, "", address, false, output, diagnostics)
}

func (i *Installation) RunProxy(ctx context.Context, domain string, output, diagnostics io.Writer) error {
	return i.run(ctx, domain, "", true, output, diagnostics)
}

func (i *Installation) run(ctx context.Context, domain, address string, proxy bool, output, diagnostics io.Writer) error {
	db, err := persistence.Open(filepath.Join(i.Directory, "backend.db"))
	if err != nil {
		return err
	}
	defer db.Close()
	certificates := filepath.Join(i.Directory, "certificates")
	if !proxy {
		if err = os.MkdirAll(certificates, 0700); err != nil {
			return err
		}
	}
	fmt.Fprintln(output, "N.O.V.A. 3 community server. Native competitive modes are enabled; gameplay parity and mounted physics remain under verification. Ctrl+C stops the server.")
	options := application.NativeServerOptions{
		Authority: i.Authority,
		TLS:       "127.1.1.1:18443", SharedPort: true,
		Directory: certificates, Ready: output, Diagnostics: diagnostics, Domain: domain, Address: address, Match: "127.1.1.1:18443",
	}
	if domain != "" {
		options.TLS = "0.0.0.0:18443"
		options.Match = "0.0.0.0:18443"
	}
	if address != "" {
		options.TLS = net.JoinHostPort(address, "18443")
		options.Match = net.JoinHostPort(address, "18443")
	}
	if proxy {
		options.ProxyTLS = true
		options.TLS = "127.0.0.1:18080"
	}
	return application.RunNative(ctx, db, i.Shop, options)
}
