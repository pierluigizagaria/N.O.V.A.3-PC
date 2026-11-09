package native

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"nova3.test/backend/internal/protocol/legacy"
)

func TestProxyTrustIsExplicitAndLocal(t *testing.T) {
	s, _, _, _ := fixture(t)
	for _, test := range []struct {
		name   string
		proxy  bool
		remote string
		token  bool
		status int
	}{
		{"forged-headers", false, "127.0.0.1:1234", false, http.StatusOK},
		{"local-proxy", true, "127.0.0.1:1234", true, http.StatusOK},
		{"remote-peer", true, "192.0.2.1:1234", false, http.StatusForbidden},
	} {
		t.Run(test.name, func(t *testing.T) {
			s.ProxyTLS = test.proxy
			r := httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte("f|15|u|alice|p|native-test-password|")), nil)
			r.RemoteAddr = test.remote
			r.Header.Set("X-Forwarded-Proto", "https")
			r.Header.Set("Forwarded", "proto=https;for=127.0.0.1")
			w := httptest.NewRecorder()
			s.Handler().ServeHTTP(w, r)
			if w.Code != test.status || (len(w.Header().Get("X-Nova-Session")) == 64) != test.token {
				t.Fatal("incorrect proxy trust boundary")
			}
		})
	}
}
