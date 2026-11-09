package community

import "testing"

func TestAddressModeIsExplicitUnicastIPv4(t *testing.T) {
	for _, value := range []string{"127.1.1.1", "192.168.1.10", "10.0.0.2", "203.0.113.10"} {
		if !ValidAddress(value) {
			t.Fatal(value)
		}
	}
	for _, value := range []string{"", "0.0.0.0", "255.255.255.255", "224.0.0.1", "::1", "::ffff:127.0.0.1", "host.test", "127.0.0.1:15201", "127.0.0.01"} {
		if ValidAddress(value) {
			t.Fatal(value)
		}
	}
}
