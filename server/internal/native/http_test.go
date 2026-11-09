package native

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"image/png"
	"io"
	"log"
	"net"
	"net/http"
	"net/http/httptest"
	"net/http/httptrace"
	"net/url"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/testfixture"
)

func fixture(t *testing.T) (*Services, string, string, *bytes.Buffer) {
	t.Helper()
	c, m := testfixture.NativeCatalogue()
	a, err := shop.New(c, m)
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), "native.db")
	s, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	if err = s.Admit(c); err != nil {
		t.Fatal(err)
	}
	account, err := accounts.Create(s, "alice")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s, account.ID, c.Version); err != nil {
		t.Fatal(err)
	}
	if err = accounts.SetPassword(s, account.ID, "native-test-password"); err != nil {
		t.Fatal(err)
	}
	diagnostics := &bytes.Buffer{}
	return &Services{Store: s, Shop: a, Endpoints: Endpoints{HTTP: "http://127.1.1.1", TLS: "https://127.1.1.1:18443", Lobby: "127.1.1.1:15200"}, Log: log.New(diagnostics, "", 0)}, account.ID, path, diagnostics
}

func request(t *testing.T, client *http.Client, method, address, body string, status int) string {
	t.Helper()
	r, err := http.NewRequest(method, address, strings.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	if method == "POST" {
		r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}
	response, err := client.Do(r)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(response.Body)
	if err != nil {
		t.Fatal(err)
	}
	if response.StatusCode != status {
		t.Fatalf("status=%d expected=%d body=%s", response.StatusCode, status, data)
	}
	return string(data)
}

func TestNativeLoginAuthorizeStorageAndRestart(t *testing.T) {
	s, account, path, diagnostics := fixture(t)
	server := httptest.NewTLSServer(s.Handler())
	defer server.Close()
	client := server.Client()
	legacyURL := func(text string) string { return server.URL + "/legacy?b=" + legacy.Encode([]byte(text)) }
	if value := request(t, client, "GET", legacyURL("f|97|u|"+account+"|"), "", 200); value != legacy.Failure(97, 40) {
		t.Fatal("presence accepted without login")
	}
	if value := request(t, client, "GET", legacyURL("f|15|u|alice|p|incorrect|"), "", 200); value != legacy.Failure(15, 40) {
		t.Fatal("invalid native login accepted")
	}
	uid := loginUID(t, request(t, client, "GET", legacyURL("f|15|u|alice|p|native-test-password|"), "", 200))
	if request(t, client, "GET", legacyURL("f|97|u|"+uid+"|"), "", 200) != legacy.Success(97, "") {
		t.Fatal("native presence acknowledgement")
	}
	metadata := request(t, client, "GET", legacyURL("f|70|u|"+uid+"|"), "", 200)
	before := time.Now().Unix()
	stamp := request(t, client, "GET", legacyURL("f|127|u|"+uid+"|"), "", 200)
	stamp = strings.TrimSuffix(strings.TrimPrefix(stamp, legacy.Success(127, "t|")), "|")
	seconds, err := strconv.ParseInt(stamp, 10, 64)
	if err != nil || seconds < before || seconds > time.Now().Unix() {
		t.Fatal("native server timestamp", err)
	}
	if !strings.Contains(metadata, "n|0|d|local-default|t|png|url|local-default|") {
		t.Fatal("native default avatar metadata", metadata)
	}
	avatar := request(t, client, "GET", legacyURL("f|101|u|"+uid+"|d|local-default|"), "", 200)
	avatar = strings.TrimPrefix(avatar, legacy.Success(101, ""))
	decoded, err := base64.StdEncoding.DecodeString(strings.TrimPrefix(avatar, "d"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err = png.Decode(bytes.NewReader(decoded)); err != nil {
		t.Fatal("invalid default avatar PNG", err)
	}
	if request(t, client, "GET", legacyURL("f|101|u|"+uid+"|d|other|"), "", 200) != legacy.Failure(101, 40) {
		t.Fatal("unknown avatar accepted")
	}
	for _, code := range []string{"170", "171"} {
		if !strings.HasSuffix(request(t, client, "GET", legacyURL("g|"+code+"|u|"+uid+"|"), "", 200), "|n") {
			t.Fatal("empty event collection")
		}
	}
	extended := url.Values{"b": {legacy.Encode([]byte("g|112|i|7|u|" + uid + "|tid|0|p|1|x|1|"))}, "v": {"4"}}
	if value := request(t, client, "POST", server.URL+"/legacy", extended.Encode(), 200); !strings.HasPrefix(value, "g|112|r|s|y|1|") {
		t.Fatal("native extended POST ranking", value)
	}
	q := url.Values{"scope": {"storage"}, "credential_type": {"gllive"}, "grant_type": {"password"}, "username": {"alice"}, "password": {"incorrect"}}
	request(t, client, "GET", server.URL+"/authorize?"+q.Encode(), "", 401)
	q.Set("password", "native-test-password")
	var authorized struct {
		Token string `json:"access_token"`
	}
	if err := json.Unmarshal([]byte(request(t, client, "GET", server.URL+"/authorize?"+q.Encode(), "", 200)), &authorized); err != nil || len(authorized.Token) != 64 {
		t.Fatal(err)
	}
	endpoint := server.URL + "/data/me/key2_01?access_token=" + authorized.Token
	request(t, client, "GET", server.URL+"/data/gllive:bob/key2_01?access_token="+authorized.Token, "", 400)
	request(t, client, "GET", server.URL+"/data/me/key2_01?access_token=invalid", "", 400)
	fields := strings.Split(request(t, client, "GET", endpoint, "", 200), "|")
	fields[8] = "6^" + strings.Repeat("2#0#0#1#0#0#2#1#0#0^", 6) + "5^0^1"
	fields[11] = "2^255^-1^1"
	updated := strings.Join(fields, "|")
	form := url.Values{"access_token": {authorized.Token}, "data": {updated}, "visibility": {"private"}}
	if request(t, client, "POST", server.URL+"/data/me/key2_01", form.Encode(), 200) != updated {
		t.Fatal("native equipment not retained")
	}
	achievementURL := server.URL + "/data/me/key3_01?access_token=" + authorized.Token
	achievements := request(t, client, "GET", achievementURL, "", 200)
	if len(achievements) != 236 {
		t.Fatal("complete original achievement seed")
	}
	achievements = achievements[:len(achievements)-1] + "1"
	form.Set("data", achievements)
	request(t, client, "POST", server.URL+"/data/me/key3_01", form.Encode(), 200)
	// Original GLXHttp puts all POST parameters in the query, with no body.
	request(t, client, "POST", server.URL+"/data/me/key3_01?"+form.Encode(), "", 200)
	request(t, client, "POST", server.URL+"/data/me/key3_01?"+form.Encode(), form.Encode(), 400)
	form.Set("data", achievements[:len(achievements)-1])
	request(t, client, "POST", server.URL+"/data/me/key3_01", form.Encode(), 400)
	if request(t, client, "GET", achievementURL, "", 200) != achievements {
		t.Fatal("invalid achievement write changed state")
	}
	server.Close()
	if err := s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	s.Store = reopened
	next := httptest.NewTLSServer(s.Handler())
	defer next.Close()
	if request(t, next.Client(), "GET", next.URL+"/data/me/key2_01?access_token="+authorized.Token, "", 200) != updated {
		t.Fatal("equipment lost on reopen")
	}
	if request(t, next.Client(), "GET", next.URL+"/data/me/key3_01?access_token="+authorized.Token, "", 200) != achievements {
		t.Fatal("achievement lost on reopen")
	}
	if strings.Contains(diagnostics.String(), "native-test-password") || strings.Contains(diagnostics.String(), authorized.Token) {
		t.Fatal("credentials leaked into server diagnostics")
	}
}

func TestNativeStorageReusesTLSConnection(t *testing.T) {
	s, account, _, _ := fixture(t)
	token, err := accounts.Issue(s.Store, account, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewTLSServer(s.Handler())
	defer server.Close()
	for index, key := range []string{"key1_01", "key3_01", "key2_01"} {
		reused := false
		r, e := http.NewRequest("GET", server.URL+"/data/me/"+key+"?access_token="+token, nil)
		if e != nil {
			t.Fatal(e)
		}
		r = r.WithContext(httptrace.WithClientTrace(r.Context(), &httptrace.ClientTrace{GotConn: func(info httptrace.GotConnInfo) { reused = info.Reused }}))
		response, e := server.Client().Do(r)
		if e != nil {
			t.Fatal(e)
		}
		_, e = io.Copy(io.Discard, response.Body)
		response.Body.Close()
		if e != nil || response.StatusCode != 200 || response.Close || (index > 0 && !reused) {
			t.Fatalf("native sequential key=%s status=%d close=%t reused=%t err=%v", key, response.StatusCode, response.Close, reused, e)
		}
	}
}

func TestNativeLobbyRequiresPrecedingLogin(t *testing.T) {
	s, account, _, _ := fixture(t)
	for _, authenticated := range []bool{false, true} {
		if authenticated {
			if _, err := s.dispatchLegacy(legacy.Request{Function: 15, Fields: map[string]string{"u": "alice", "p": "native-test-password"}}); err != nil {
				t.Fatal(err)
			}
		}
		client, server := net.Pipe()
		done := make(chan struct{})
		go func() { s.lobbyConnection(server); close(done) }()
		client.SetDeadline(time.Now().Add(3 * time.Second))
		if err := lobby.Write(client, lobby.Frame{Opcode: 1, Payload: []byte{0, 0, 0, 7}}); err != nil {
			t.Fatal(err)
		}
		response, err := lobby.Read(client)
		if err != nil || response.Opcode != 2 {
			t.Fatal(response, err)
		}
		payload := make([]byte, 4)
		binary.BigEndian.PutUint32(payload, 7)
		payload = append(payload, lobby.String(account)...)
		payload = append(payload, 0, 0, 0, 0, 1)
		if err = lobby.Write(client, lobby.Frame{Opcode: 0x86, Payload: payload}); err != nil {
			t.Fatal(err)
		}
		response, err = lobby.Read(client)
		if authenticated {
			if err != nil || response.Opcode != 6 || binary.BigEndian.Uint32(response.Payload) != 0 {
				t.Fatal(response, err)
			}
			if err = lobby.Write(client, lobby.Frame{Opcode: lobby.KeepAliveRequest}); err != nil {
				t.Fatal(err)
			}
			keepalive, e := lobby.Read(client)
			if e != nil || keepalive.Opcode != lobby.KeepAliveResponse || len(keepalive.Payload) != 4 {
				t.Fatal(keepalive, e)
			}
			count, e := s.dispatchLegacy(legacy.Request{Function: 116, Fields: map[string]string{"u": "Gameloft"}})
			if e != nil || count != "1|" {
				t.Fatal("active lobby count", count, e)
			}
		} else if err == nil {
			t.Fatal("lobby accepted unauthenticated identity")
		}
		client.Close()
		select {
		case <-done:
		case <-time.After(3 * time.Second):
			t.Fatal("lobby handler did not stop")
		}
		count, e := s.dispatchLegacy(legacy.Request{Function: 116, Fields: map[string]string{"u": "Gameloft"}})
		if e != nil || count != "0|" {
			t.Fatal("disconnected lobby count", count, e)
		}
	}
}
