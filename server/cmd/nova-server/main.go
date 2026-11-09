// nova-server is the independent dedicated server entrypoint.
package main

import (
	"bufio"
	"fmt"
	"os"
	"runtime"
)

func main() {
	executable, err := os.Executable()
	if err == nil {
		err = configuredRun(os.Args[1:], executable)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		if len(os.Args) == 1 && runtime.GOOS == "windows" {
			fmt.Fprintln(os.Stderr, "Run nova-server --check to validate installed server data. Press Enter to close.")
			_, _ = bufio.NewReader(os.Stdin).ReadString('\n')
		}
		os.Exit(1)
	}
}
