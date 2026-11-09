//go:build !debug

package main

import (
	"errors"
	"fmt"
	"os"
)

const publicHelp = `N.O.V.A. 3 server
Usage: nova-server [--domain HOST | --proxy-domain HOST | --address IPv4 | --check | --help]

Start without arguments. Installed server data lives in data/ beside this executable.
--check validates installed data without starting services.
--domain serves HTTPS, authenticated lobby and game UDP on all IPv4 interfaces.
Install the domain certificate as data/certificates/fullchain.pem and privkey.pem.
--proxy-domain delegates TLS to a reverse proxy on this host; no server certificate needed.
Proxy upstreams: HTTP 127.0.0.1:18080, authenticated TCP lobby 127.0.0.1:15000.
The proxy must expose TLS on TCP 18443 and 15200. Game UDP 15201 stays direct.
--address binds to that local IPv4 address for LAN testing with the existing IP client mode.
The client does not verify certificates for an explicitly selected literal IPv4 address.
Ports: TCP 18443 (HTTPS), TCP 15200 (lobby), UDP 15201 (game).
This build includes a free-for-all on-foot combat preview.
Targets may hold any catalogue weapon.
Remaining weapon variants, vehicles and authoritative movement physics are incomplete.
`

func runUserCommand(args []string) error {
	if len(args) == 1 && (args[0] == "--help" || args[0] == "-h") {
		fmt.Fprint(os.Stdout, publicHelp)
		return nil
	}
	return errors.New("unsupported option; use --help")
}
