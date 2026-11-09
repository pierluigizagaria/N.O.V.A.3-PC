package native

import (
	"encoding/json"
	"net/http/httptest"
	"testing"

	"nova3.test/backend/internal/protocol/legacy"
)

// Freeze the existing interfaces: endpoint discovery belongs to the original
// bootstrap/locate/lobby responses, not to a second client configuration API.
func TestOriginalDiscoveryInterfaces(t *testing.T) {
	s, account, _, _ := fixture(t)
	s.Endpoints = Endpoints{HTTP: "http://127.1.1.2:18080", TLS: "https://127.1.1.2:18444", Lobby: "127.1.1.2:15201"}
	handler := s.Handler()
	get := func(target string, status int) string {
		t.Helper()
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, httptest.NewRequest("GET", target, nil))
		if response.Code != status {
			t.Fatalf("%s: status=%d body=%s", target, response.Code, response.Body.String())
		}
		return response.Body.String()
	}
	config := get("/ope/ServerConfig.php?b="+legacy.Encode([]byte("f|1|")), 200)
	if config != legacy.Success(1, "XplayerURL|"+s.Endpoints.TLS+"/legacy|type|0|XPPHPVerNo|4|latest|1.0.7|") {
		t.Fatal("original pipe config envelope/endpoint", config)
	}
	var eve map[string]string
	if err := json.Unmarshal([]byte(get("/config/synthetic", 200)), &eve); err != nil ||
		len(eve) != 2 || eve["pandora"] != s.Endpoints.HTTP || eve["status"] != "up" {
		t.Fatal("original Eve response", eve, err)
	}
	for _, service := range []string{"auth", "storage"} {
		if get("/locate?service="+service, 200) != "127.1.1.2:18444" {
			t.Fatal("locate must return host:port, without scheme or JSON")
		}
	}
	get("/locate?service=unknown", 400)
	get("/discovery", 404)
	call := func(fields string) string { return get("/legacy?b="+legacy.Encode([]byte(fields)), 200) }
	if call("f|107|u|"+account+"|") != legacy.Failure(107, 40) {
		t.Fatal("lobby lookup bypassed native login")
	}
	uid := loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	for _, fn := range []struct {
		text string
		code int
	}{{"107", 107}, {"108", 108}} {
		if call("f|"+fn.text+"|u|"+uid+"|") != legacy.Success(fn.code, s.Endpoints.Lobby) {
			t.Fatal("original lobby lookup changed")
		}
	}
}
