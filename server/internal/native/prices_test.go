package native

import (
	"encoding/xml"
	"net/http/httptest"
	"testing"
)

func TestBuiltInPriceRouteWithoutRequestFile(t *testing.T) {
	// Literal client request: no route assignment, file or configuration needed.
	const target = "/partners/offline_ingame/item_list.php?product=1224"
	s, _, _, _ := fixture(t)
	server := httptest.NewServer(s.Handler())
	defer server.Close()
	body := request(t, server.Client(), "GET", server.URL+target, "", 200)
	expected, err := s.Shop.Prices()
	if err != nil || body != string(expected) {
		t.Fatal("native price response differs from mapping", err)
	}
	var document struct {
		XMLName xml.Name `xml:"offline_item_prices"`
		Items   []struct {
			Price int `xml:"price_value"`
		} `xml:"items>item"`
	}
	if err = xml.Unmarshal([]byte(body), &document); err != nil || len(document.Items) == 0 {
		t.Fatal("client requires XML item prices", err)
	}
	for _, path := range []string{target + "&extra=1", target + "&product=1224", "/partners/offline_ingame/item_list.php?product=7", "/partners/offline_ingame/item_list.php", "/prices"} {
		request(t, server.Client(), "GET", server.URL+path, "", 404)
	}
	request(t, server.Client(), "POST", server.URL+target, "", 405)
}
