// Package native serves the client's existing legacy and federation protocols.
package native

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/buildmode"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/sessions"
)

type Endpoints struct {
	HTTP  string
	TLS   string
	Lobby string
}

// The 1.0.7 client keeps this request target when connecting to our server.
// It is an inbound protocol route, not a URL to contact or an operator setting.
const priceRequestTarget = "/partners/offline_ingame/item_list.php?product=1224"

type Services struct {
	Authority *authority.Definitions
	ProxyTLS  bool // Explicit same-host TLS terminator; never inferred from request headers.
	Store     *persistence.Store
	Shop      *shop.Adapter
	Endpoints Endpoints
	Log       *log.Logger
	Sessions  sessions.Legacy
	rooms     roomService
}

func reply(w http.ResponseWriter, status int, body string) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	if w.Header().Get("Connection") == "" {
		w.Header().Set("Connection", "close")
	}
	w.WriteHeader(status)
	io.WriteString(w, body)
}

func (s *Services) Handler() http.Handler {
	compatibility := shop.Handler(s.Store, s.Shop)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if s.ProxyTLS && !loopbackPeer(r.RemoteAddr) {
			reply(w, http.StatusForbidden, "reverse proxy upstream requires loopback")
			return
		}
		switch {
		case r.URL.Path == "/pc/consumables":
			s.consumableInventory(w, r)
		case r.URL.RequestURI() == priceRequestTarget:
			if r.Method != "GET" {
				reply(w, 405, "method not allowed")
				return
			}
			body, err := s.Shop.Prices()
			if err != nil {
				reply(w, 500, "price catalogue unavailable")
				return
			}
			w.Header().Set("X-Nova-Catalogue", s.Shop.Catalogue.Version)
			s.Log.Printf("native price request accepted")
			reply(w, 200, string(body))
		case buildmode.Developer && (strings.HasPrefix(r.URL.Path, "/compat/") || r.URL.Path == "/health"):
			compatibility.ServeHTTP(w, r)
		case strings.HasPrefix(r.URL.Path, "/config/"):
			if r.Method != "GET" {
				reply(w, 405, "method not allowed")
				return
			}
			s.Log.Printf("native eve configuration")
			body, _ := json.Marshal(map[string]string{"pandora": s.Endpoints.HTTP, "status": "up"})
			reply(w, 200, string(body))
		case r.URL.Path == "/locate":
			service := r.URL.Query().Get("service")
			if r.Method != "GET" || (service != "auth" && service != "storage") {
				reply(w, 400, "unsupported service")
				return
			}
			s.Log.Printf("native locate service=%s", service)
			reply(w, 200, strings.TrimPrefix(s.Endpoints.TLS, "https://"))
		case r.URL.Path == "/authorize":
			s.authorize(w, r)
		case strings.HasPrefix(r.URL.Path, "/data/"):
			s.data(w, r)
		case r.URL.Query().Has("b") || (r.Method == "POST" && r.URL.Path == "/legacy"):
			s.legacy(w, r)
		default:
			s.Log.Printf("native HTTP unsupported method=%s path=%q", r.Method, r.URL.Path)
			reply(w, 404, "unsupported native operation")
		}
	})
}

func (s *Services) authorize(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	if r.Method != "GET" || q.Get("scope") != "storage" || q.Get("credential_type") != "gllive" || q.Get("grant_type") != "password" {
		reply(w, 400, "unsupported authorize request")
		return
	}
	id, err := accounts.Authenticate(s.Store, q.Get("username"), q.Get("password"))
	if err != nil {
		reply(w, 401, "invalid credentials")
		return
	}
	token, err := accounts.Issue(s.Store, id, time.Hour)
	if err != nil {
		reply(w, 500, "cannot issue session")
		return
	}
	s.Log.Printf("native storage authorization accepted")
	body, _ := json.Marshal(map[string]string{"access_token": token})
	reply(w, 200, string(body))
}

func (s *Services) data(w http.ResponseWriter, r *http.Request) {
	// Native GLXHttp reuses its storage connection between sequential keys.
	// Content-Length supplies framing without closing the underlying TLS stream.
	w.Header().Set("Connection", "keep-alive")
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/data/"), "/")
	if len(parts) != 2 || (r.Method != "GET" && r.Method != "POST") {
		reply(w, 400, "unsupported storage request")
		return
	}
	q := r.URL.Query()
	var update *string
	if r.Method == "POST" {
		body, err := io.ReadAll(io.LimitReader(r.Body, 32769))
		if err != nil || len(body) > 32768 {
			reply(w, 400, "invalid storage body")
			return
		}
		// Native GLXHttp sends POST fields in the URL with an empty body.
		// Also accept ordinary form bodies, rejecting ambiguous duplicates.
		form, err := url.ParseQuery(string(body))
		if err == nil {
			for key, values := range form {
				if q.Has(key) {
					err = fmt.Errorf("duplicate storage field")
					break
				}
				q[key] = values
			}
		}
		if err != nil || len(q["data"]) != 1 {
			s.Log.Printf("native storage invalid POST form data_fields=%d bytes=%d", len(q["data"]), len(body))
			reply(w, 400, "missing storage data")
			return
		}
		value := q.Get("data")
		update = &value
	}
	if len(q["access_token"]) != 1 {
		s.Log.Printf("native storage missing session method=%s", r.Method)
		reply(w, 401, "session required")
		return
	}
	value, err := (Storage{DB: s.Store, Shop: s.Shop}).Access(q.Get("access_token"), parts[0], parts[1], update)
	if err != nil {
		s.Log.Printf("native storage rejected method=%s key=%s reason=%v", r.Method, parts[1], err)
		reply(w, 400, "storage request rejected")
		return
	}
	s.Log.Printf("native storage method=%s key=%s persisted=%t", r.Method, parts[1], update != nil)
	reply(w, 200, value)
}

func (s *Services) legacy(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	if r.Method == "POST" {
		body, err := io.ReadAll(io.LimitReader(r.Body, 32769))
		if err != nil || len(body) > 32768 {
			reply(w, 400, "invalid native POST body")
			return
		}
		form, err := url.ParseQuery(string(body))
		if err != nil {
			reply(w, 400, "invalid native POST form")
			return
		}
		q = form
	}
	if (r.Method != "GET" && r.Method != "POST") || len(q["b"]) != 1 {
		reply(w, 400, "invalid legacy request")
		return
	}
	decoded, err := legacy.Decode(q.Get("b"))
	if err != nil {
		reply(w, 400, "invalid legacy encoding")
		return
	}
	request, err := legacy.Parse(decoded)
	if err != nil {
		reply(w, 400, "invalid legacy fields")
		return
	}
	var body string
	if request.Function == 15 {
		var id string
		id, err = s.loginLegacy(request)
		if err == nil {
			var number int64
			number, _, err = accounts.PublicIdentity(s.Store, id)
			if err == nil {
				var token string
				token, err = accounts.Issue(s.Store, id, time.Hour)
				if err == nil {
					// The original client carries u on every authenticated request.
					// Use it as a revocable bearer; un is the public player number.
					body = "u|" + token + "|t|1|un|" + strconv.FormatInt(number, 10) + "|"
					if r.TLS != nil || s.ProxyTLS {
						w.Header().Set("X-Nova-Account", id)
						w.Header().Set("X-Nova-Session", token)
					}
				}
			}
		}
	} else {
		public := request.Function == 1 || request.Function == 11 || (request.Function == 116 && request.Fields["u"] == "Gameloft")
		if !public {
			credential := request.Fields["u"]
			request.Fields["u"], err = accounts.Resolve(s.Store, credential)
			// A valid persisted bearer survives a server restart. Restore only
			// its ephemeral presence; requiring a new password login here leaves
			// the original client's signed-in menu reporting no connectivity.
			// Resolve still rejects expired/revoked credentials before admission.
			if err == nil && !s.Sessions.Valid(request.Fields["u"], time.Now()) {
				err = s.Sessions.Login(request.Fields["u"], time.Now(), time.Hour)
			}
			if err == nil && request.Function == 16 {
				err = accounts.Refresh(s.Store, credential, time.Hour)
			}
		}
		if err == nil {
			body, err = s.dispatchLegacy(request)
		}
	}
	if err != nil {
		s.Log.Printf("native legacy function=%d rejected", request.Function)
		reply(w, 200, request.Failure(40))
		return
	}
	// Native lobby/social polling is routine traffic, not a diagnostic event.
	if request.Function != 16 && request.Function != 73 && request.Function != 74 && request.Function != 116 {
		s.Log.Printf("native legacy function=%d accepted", request.Function)
	}
	reply(w, 200, request.Success(body))
}

func (s *Services) loginLegacy(r legacy.Request) (string, error) {
	id, err := accounts.Authenticate(s.Store, r.Fields["u"], r.Fields["p"])
	if err != nil {
		return "", err
	}
	if err = s.Sessions.Login(id, time.Now(), time.Hour); err != nil {
		return "", err
	}
	return id, nil
}

func (s *Services) dispatchLegacy(r legacy.Request) (string, error) {
	if r.Function == 11 {
		// Registration is followed by the original UI's ordinary password login.
		// Email delivery/recovery is not implemented by the community service.
		flag, err := nativeInteger(r.Fields, "ef", 0, 0, 1)
		if err != nil {
			return "", err
		}
		_, err = accounts.RegisterWithInformation(s.Store, r.Fields["u"], r.Fields["p"], s.Shop.Catalogue.Version,
			map[string]string{"email": r.Fields["e"], "country": r.Fields["c"], "email_flag": strconv.Itoa(flag)})
		return "", err
	}
	if r.Function == 1 {
		// The original login constructs CSSLSocket and preserves an explicit
		// port from XplayerURL. Advertising the plaintext HTTP port here sends
		// its TLS handshake to the wrong listener.
		return "XplayerURL|" + s.Endpoints.TLS + "/legacy|type|0|XPPHPVerNo|4|latest|1.0.7|", nil
	}
	if r.Function == 15 {
		id, err := s.loginLegacy(r)
		if err != nil {
			return "", err
		}
		return "u|" + id + "|t|1|un|1|", nil
	}
	// The original online-count request uses the public service identity,
	// rather than the signed-in player's UID.
	if r.Function == 116 && r.Fields["u"] == "Gameloft" {
		count := s.Sessions.Count(time.Now())
		return strconv.Itoa(count) + "|", nil
	}
	if !s.legacyAccount(r.Fields["u"]) {
		return "", fmt.Errorf("native login required")
	}
	switch r.Function {
	case 14:
		// Only mode zero is decoded. Existing names and identities are never
		// taken over; the optional native mode remains unsupported.
		if r.Fields["o"] != "0" || r.Fields["n"] != "" || r.Fields["un"] != "" {
			return "", fmt.Errorf("ordinary owner rename required")
		}
		if err := accounts.ChangeName(s.Store, r.Fields["u"], r.Fields["p"], r.Fields["nu"]); err != nil {
			return "", err
		}
		s.Sessions.Logout(r.Fields["u"])
		return "", nil
	case 50:
		if r.Fields["np"] != r.Fields["vp"] {
			return "", fmt.Errorf("password confirmation does not match")
		}
		if err := accounts.ChangePassword(s.Store, r.Fields["u"], r.Fields["p"], r.Fields["np"]); err != nil {
			return "", err
		}
		s.Sessions.Logout(r.Fields["u"])
		return "", nil
	case 16:
		return "", s.Sessions.Renew(r.Fields["u"], time.Now(), time.Hour)
	case 17:
		if err := accounts.RevokeAccount(s.Store, r.Fields["u"]); err != nil {
			return "", err
		}
		s.Sessions.Logout(r.Fields["u"])
		return "", nil
	case 97:
		state, err := nativeInteger(r.Fields, "a", 3, 0, 3)
		if err != nil {
			return "", err
		}
		return "", s.Sessions.SetState(r.Fields["u"], state, time.Now())
	case 107:
		return s.Endpoints.Lobby, nil
	case 108:
		if err := s.rooms.invitationLocation(r.Fields["u"], r.Fields["cn"], time.Now()); err != nil {
			return "", err
		}
		return s.Endpoints.Lobby, nil
	case 127:
		return "t|" + strconv.FormatInt(time.Now().Unix(), 10) + "|", nil
	case 59, 60, 61, 62, 73:
		return s.friends(r)
	case 64:
		return s.userPresence(r)
	case 65, 66, 72:
		return s.userData(r)
	case 51, 53, 54, 58, 81, 82:
		return s.userInformation(r)
	case 74, 75, 76, 77, 78, 91, 92, 93, 94:
		return s.messages(r)
	case 70, 71, 100, 101, 102:
		return s.avatars(r)
	case 670, 671:
		// Original event/prize readers recognize n as an empty collection.
		return "n", nil
	case 612, 613, 614:
		return s.ranking(r)
	default:
		return "", fmt.Errorf("unsupported native legacy function %d", r.Function)
	}
}

func (s *Services) legacyAccount(id string) bool {
	if !s.Sessions.Valid(id, time.Now()) {
		return false
	}
	var version string
	return s.Store.DB.QueryRow("SELECT version FROM profiles WHERE account=?", id).Scan(&version) == nil && version == s.Shop.Catalogue.Version
}
