package shop

import (
	"database/sql"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"strconv"
	"strings"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/persistence"
)

// ListenLocal deliberately accepts only a literal IPv4 loopback bind.
func ListenLocal(address string) (net.Listener, error) {
	host, port, err := net.SplitHostPort(address)
	if err != nil || host != "127.0.0.1" {
		return nil, errors.New("listen address must be 127.0.0.1:PORT")
	}
	n, err := strconv.Atoi(port)
	if err != nil || n < 0 || n > 65535 {
		return nil, errors.New("invalid port")
	}
	return net.Listen("tcp4", address)
}
func Handler(store *persistence.Store, a *Adapter) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		if r.URL.EscapedPath() != r.URL.Path {
			http.Error(w, "invalid path", 400)
			return
		}
		if r.URL.Path == "/health" {
			if r.Method != "GET" {
				http.Error(w, "method not allowed", 405)
				return
			}
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(map[string]string{"service": "nova-test-shop", "version": a.Catalogue.Version})
			return
		}
		path := r.URL.Path
		if path != "/compat/v1/prices" && path != "/compat/v1/purchases" && path != "/compat/v1/profile" {
			http.NotFound(w, r)
			return
		}
		if r.Method != "GET" && !(path == "/compat/v1/purchases" && r.Method == "PUT") {
			http.Error(w, "method not allowed", 405)
			return
		}
		auth := r.Header.Get("Authorization")
		if !strings.HasPrefix(auth, "Bearer ") || len(auth) != 71 {
			http.Error(w, "session required", 401)
			return
		}
		token := strings.TrimPrefix(auth, "Bearer ")
		query := r.URL.Query()
		if len(query) != 1 || len(query["version"]) != 1 {
			http.Error(w, "only one version parameter is accepted", 400)
			return
		}
		body := ""
		if r.Method == "PUT" {
			data, err := io.ReadAll(io.LimitReader(r.Body, 1025))
			if err != nil || len(data) > 1024 {
				http.Error(w, "invalid body", 400)
				return
			}
			body = string(data)
		}
		status := 500
		var result []byte
		content := "text/plain; charset=utf-8"
		err := store.Write(func(tx *sql.Tx) error {
			account, e := accounts.ResolveWith(tx, token)
			if e != nil {
				status = 401
				return e
			}
			if e = a.CheckProfile(tx, account, query.Get("version")); e != nil {
				status = 409
				return e
			}
			switch path {
			case "/compat/v1/prices":
				content = "application/xml; charset=utf-8"
				result, e = a.Prices()
			case "/compat/v1/profile":
				content = "application/json"
				result, e = json.Marshal(map[string]any{"version": a.Catalogue.Version, "xp": a.Mapping.Policy.XP, "rank": a.Mapping.Policy.Rank, "native_delivery": "not-integrated"})
			case "/compat/v1/purchases":
				if r.Method == "PUT" {
					if e = a.Write(tx, account, body); e != nil {
						status = 400
						return e
					}
				}
				var value string
				value, e = a.Read(tx, account)
				result = []byte(value)
			}
			return e
		})
		if err != nil {
			http.Error(w, http.StatusText(status), status)
			return
		}
		w.Header().Set("Content-Type", content)
		w.Header().Set("X-Nova-Catalogue", a.Catalogue.Version)
		w.WriteHeader(200)
		w.Write(result)
	})
}
