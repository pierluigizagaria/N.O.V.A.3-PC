package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"nova3.test/backend/internal/installation"
	"nova3.test/backend/internal/transport/community"
)

func configuredRun(args []string, executable string) error {
	check := len(args) == 1 && args[0] == "--check"
	domain := ""
	address := ""
	proxy := len(args) == 2 && args[0] == "--proxy-domain"
	if len(args) == 2 && (args[0] == "--domain" || proxy) {
		domain = args[1]
		if !community.ValidDomain(domain) {
			return errors.New("domain must be a lowercase DNS hostname without a scheme, port or path")
		}
	}
	if len(args) == 2 && args[0] == "--address" {
		address = args[1]
		if !community.ValidAddress(address) {
			return errors.New("address must be a unicast IPv4 address without a port")
		}
	}
	if len(args) != 0 && !check && domain == "" && address == "" {
		return runUserCommand(args)
	}
	installed, err := installation.Load(executable)
	if err != nil {
		return err
	}
	if check {
		fmt.Fprintln(os.Stdout, "Installed server data is valid. Native competitive modes are enabled; gameplay parity remains under verification.")
		return nil
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if address != "" {
		return installed.RunAddress(ctx, address, os.Stdout, os.Stderr)
	}
	if proxy {
		return installed.RunProxy(ctx, domain, os.Stdout, os.Stderr)
	}
	return installed.RunDomain(ctx, domain, os.Stdout, os.Stderr)
}
