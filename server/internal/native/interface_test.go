package native

import (
	"net/http/httptest"
	"nova3.test/backend/internal/buildmode"
	"testing"
)

func TestCompatibilityRoutesFollowBuildInterface(t *testing.T) {
	s, _, _, _ := fixture(t)
	for _, target := range []string{"/health", "/compat/v1/prices?version=fixture"} {
		response := httptest.NewRecorder()
		s.Handler().ServeHTTP(response, httptest.NewRequest("GET", target, nil))
		if !buildmode.Developer && (response.Code != 404 || response.Body.String() != "unsupported native operation") {
			t.Fatal("Release exposed a compatibility route", target, response.Code, response.Body.String())
		}
		if buildmode.Developer && target == "/health" && response.Code != 200 {
			t.Fatal("Debug health route unavailable")
		}
	}
}
