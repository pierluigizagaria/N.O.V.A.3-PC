// Package shop implements bounded original purchase/price bodies, not service envelopes.
package shop

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"encoding/xml"
	"errors"
	"io"
	"reflect"
	"sort"
	"strconv"
	"strings"

	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/loadout"
)

var OwnershipGroups = []string{"primary", "secondary", "addons", "perks", "items", "killchains", "signatures", "presets"}

// The original armory always allocates six records, even for locked slots.
// Two slots are included; the catalogue describes only the four purchasable ones.
const nativePresetSlots = 6
const includedPresetSlots = 2

type Row struct {
	ServerID      string              `json:"server_id"`
	Native        catalogue.NativeRef `json:"native"`
	Label         string              `json:"label"`
	MinimumLevel  *int                `json:"minimum_level"`
	RequiredRank  *int                `json:"required_rank"`
	ResourcePrice int                 `json:"resource_price"`
	ResourceOwned *bool               `json:"resource_owned"`
}
type Policy struct {
	XP              int  `json:"xp"`
	Rank            int  `json:"rank"`
	BonusQuantity   int  `json:"bonus_quantity"`
	BonusTestPolicy bool `json:"bonus_quantity_is_test_policy"`
}
type Mapping struct {
	Library  string `json:"library"`
	Resource string `json:"resource"`
	Rows     []Row  `json:"rows"`
	Policy   Policy `json:"policy"`
}
type Adapter struct {
	Catalogue catalogue.Catalogue
	Mapping   Mapping
	Groups    map[string][]catalogue.Item
	Rows      map[string]Row
}

func DecodeMapping(data []byte) (Mapping, error) {
	var m Mapping
	if len(data) > 1024*1024 {
		return m, errors.New("mapping too large")
	}
	d := json.NewDecoder(bytes.NewReader(data))
	d.DisallowUnknownFields()
	if err := d.Decode(&m); err != nil {
		return m, err
	}
	if d.Decode(new(any)) != io.EOF {
		return m, errors.New("trailing mapping data")
	}
	return m, nil
}
func New(c catalogue.Catalogue, m Mapping) (*Adapter, error) {
	if err := c.Validate(); err != nil {
		return nil, err
	}
	if c.Source != "native" || c.LibrarySHA256 != m.Library || c.ResourceSHA256 != m.Resource {
		return nil, errors.New("mapping identity mismatch")
	}
	if m.Policy.XP < 0 || m.Policy.XP > 2147483647 || m.Policy.Rank < 0 || m.Policy.Rank > 1000 || m.Policy.BonusQuantity < 0 || m.Policy.BonusQuantity > 1000 {
		return nil, errors.New("invalid test progression")
	}
	a := &Adapter{Catalogue: c, Mapping: m, Groups: map[string][]catalogue.Item{}, Rows: map[string]Row{}}
	for _, r := range m.Rows {
		if _, exists := a.Rows[r.ServerID]; exists {
			return nil, errors.New("duplicate mapping row")
		}
		a.Rows[r.ServerID] = r
	}
	if len(m.Rows) != len(c.Items) {
		return nil, errors.New("mapping count mismatch")
	}
	for _, i := range c.Items {
		r, ok := a.Rows[i.ID]
		if !ok || i.Native == nil || *i.Native != r.Native || r.ResourcePrice < 0 || r.ResourcePrice > 2147483647 {
			return nil, errors.New("invalid mapped row")
		}
		if r.MinimumLevel != nil && (*r.MinimumLevel < 0 || *r.MinimumLevel > 1000) {
			return nil, errors.New("invalid requirement")
		}
		if i.Native.Group != "consumables" && r.MinimumLevel == nil {
			return nil, errors.New("missing row requirement")
		}
		a.Groups[i.Native.Group] = append(a.Groups[i.Native.Group], i)
	}
	for _, g := range append(append([]string{}, OwnershipGroups...), "consumables") {
		items := a.Groups[g]
		if len(items) == 0 || len(items) > 128 {
			return nil, errors.New("unsupported wire group size")
		}
		sort.Slice(items, func(i, j int) bool { return items[i].Native.Ordinal < items[j].Native.Ordinal })
		a.Groups[g] = items
	}
	if len(a.Groups["consumables"]) != 9 || len(a.Groups["presets"]) > 4 {
		return nil, errors.New("unsupported native capacities")
	}
	// The native signature tail consumes one first row and shares the next row.
	sig := a.Groups["signatures"]
	if len(sig) > 2 {
		first := a.Rows[sig[1].ID]
		for _, i := range sig[2:] {
			r := a.Rows[i.ID]
			if r.ResourcePrice != first.ResourcePrice || !reflect.DeepEqual(r.MinimumLevel, first.MinimumLevel) {
				return nil, errors.New("signature tail cannot be represented faithfully")
			}
		}
	}
	return a, nil
}

type priceItem struct {
	Price       int `xml:"price_value"`
	Requirement int `xml:"previous_price_value"`
}

func (a *Adapter) Prices() ([]byte, error) {
	rows := []priceItem{}
	order := []string{"primary", "secondary", "addons", "perks", "items", "killchains", "consumables", "presets", "signatures"}
	for _, g := range order {
		for n, i := range a.Groups[g] {
			if g == "signatures" && n > 1 {
				break
			}
			r := a.Rows[i.ID]
			req := 0
			if r.MinimumLevel != nil {
				req = *r.MinimumLevel
			}
			if g == "consumables" {
				req = a.Mapping.Policy.BonusQuantity
			}
			rows = append(rows, priceItem{r.ResourcePrice, req})
		}
	}
	doc := struct {
		XMLName xml.Name    `xml:"offline_item_prices"`
		Items   []priceItem `xml:"items>item"`
	}{Items: rows}
	return xml.Marshal(doc)
}
func (a *Adapter) CheckProfile(tx *sql.Tx, account, version string) error {
	if version != a.Catalogue.Version {
		return errors.New("stale catalogue version")
	}
	var stored string
	if err := tx.QueryRow("SELECT version FROM profiles WHERE account=?", account).Scan(&stored); err != nil {
		return err
	}
	if stored != version {
		return errors.New("account catalogue mismatch")
	}
	return nil
}
func (a *Adapter) Inventory(tx *sql.Tx, account string) (map[string]int, error) {
	rows, err := tx.Query("SELECT item,quantity FROM entitlements WHERE account=?", account)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := map[string]int{}
	for rows.Next() {
		var id string
		var n int
		if err = rows.Scan(&id, &n); err != nil {
			return nil, err
		}
		out[id] = n
	}
	return out, rows.Err()
}
func (a *Adapter) fields(inventory map[string]int) []string {
	fields := make([]string, 12)
	fields[0] = "0"
	for n, g := range OwnershipGroups {
		var b strings.Builder
		for _, i := range a.Groups[g] {
			if _, owned := inventory[i.ID]; owned {
				b.WriteByte('T')
			} else {
				b.WriteByte('F')
			}
		}
		index := n + 1
		if n == 7 {
			index = 10
		}
		fields[index] = b.String()
	}
	qty := []string{}
	for _, i := range a.Groups["consumables"] {
		qty = append(qty, strconv.Itoa(inventory[i.ID]))
	}
	fields[9] = strings.Join(qty, "^")
	return fields
}
func (a *Adapter) Read(tx *sql.Tx, account string) (string, error) {
	inventory, err := a.Inventory(tx, account)
	if err != nil {
		return "", err
	}
	fields := a.fields(inventory)
	var coins int
	if err = tx.QueryRow("SELECT coins FROM wallets WHERE account=?", account).Scan(&coins); err != nil && !errors.Is(err, sql.ErrNoRows) {
		return "", err
	}
	fields[0] = strconv.Itoa(coins)
	err = tx.QueryRow("SELECT equipment,skins FROM native_equipment WHERE account=?", account).Scan(&fields[8], &fields[11])
	if errors.Is(err, sql.ErrNoRows) {
		var raw string
		selection := loadout.Selection{}
		e := tx.QueryRow("SELECT body FROM loadouts WHERE account=?", account).Scan(&raw)
		if e != nil && !errors.Is(e, sql.ErrNoRows) {
			return "", e
		}
		if e == nil {
			if e = json.Unmarshal([]byte(raw), &selection); e != nil {
				return "", e
			}
		}
		record := [10]int{}
		for _, v := range []struct {
			slot, group string
			position    int
		}{{"primary", "primary", 0}, {"secondary", "secondary", 3}, {"grenade", "items", 6}, {"perk", "perks", 7}} {
			id := selection[v.slot]
			if id == "" && (v.slot == "primary" || v.slot == "secondary") {
				for _, i := range a.Groups[v.group] {
					if _, ok := inventory[i.ID]; ok {
						id = i.ID
						break
					}
				}
			}
			for _, i := range a.Groups[v.group] {
				if i.ID == id {
					record[v.position] = i.Native.Ordinal + 1
					break
				}
			}
		}
		parts := []string{}
		for _, n := range record {
			parts = append(parts, strconv.Itoa(n))
		}
		fields[8] = "1^" + strings.Join(parts, "#") + "^0^0^0"
		fields[11] = ""
	} else if err != nil {
		return "", err
	}
	if _, err = a.parseEquipment(fields[8], inventory); err != nil {
		return "", err
	}
	fields[8] = expandEquipment(fields[8])
	if err = a.checkSkins(fields[11]); err != nil {
		return "", err
	}
	return strings.Join(fields, "|"), nil
}

// Expand older short records before the original menu sees them: otherwise its
// six-slot layout resets every selection. Keep existing records and the active
// slot/signature/background unchanged; seed missing slots from the first valid
// record. Reads leave the database intact until a native save persists the body.
// The caller must validate the record first.
func expandEquipment(body string) string {
	p := strings.Split(body, "^")
	count := len(p) - 4
	if count == nativePresetSlots {
		return body
	}
	result := []string{strconv.Itoa(nativePresetSlots)}
	result = append(result, p[1:count+1]...)
	for n := count; n < nativePresetSlots; n++ {
		result = append(result, p[1])
	}
	result = append(result, p[count+1:]...)
	return strings.Join(result, "^")
}

func number(s string, min, max int) (int, error) {
	n, e := strconv.ParseInt(s, 10, 32)
	if e != nil || strconv.FormatInt(n, 10) != s || n < int64(min) || n > int64(max) {
		return 0, errors.New("invalid bounded decimal")
	}
	return int(n), nil
}
func (a *Adapter) parseEquipment(body string, inventory map[string]int) (loadout.Selection, error) {
	if len(body) == 0 || len(body) > 240 {
		return nil, errors.New("missing or oversized equipment")
	}
	p := strings.Split(body, "^")
	count, e := number(p[0], 1, nativePresetSlots)
	if e != nil || len(p) != count+4 {
		return nil, errors.New("invalid preset count")
	}
	active, e := number(p[count+1], 0, count-1)
	if e != nil {
		return nil, e
	}
	if active >= includedPresetSlots {
		index := active - includedPresetSlots
		presets := a.Groups["presets"]
		if index >= len(presets) {
			return nil, errors.New("unavailable active preset")
		}
		if _, owned := inventory[presets[index].ID]; !owned {
			return nil, errors.New("unowned active preset")
		}
	}
	if _, e = number(p[count+2], 0, len(a.Groups["signatures"])-1); e != nil {
		return nil, e
	}
	if _, e = number(p[count+3], 0, 1); e != nil {
		return nil, errors.New("invalid native signature background")
	}
	groups := []string{"primary", "addons", "addons", "secondary", "addons", "addons", "items", "perks", "perks"}
	selected := loadout.Selection{}
	for n := 0; n < count; n++ {
		values := strings.Split(p[n+1], "#")
		if len(values) != 10 {
			return nil, errors.New("invalid preset record")
		}
		for field, g := range groups {
			idx, err := number(values[field], 0, len(a.Groups[g]))
			if err != nil {
				return nil, err
			}
			if idx > 0 {
				item := a.Groups[g][idx-1]
				if _, ok := inventory[item.ID]; !ok {
					return nil, errors.New("unowned preset item")
				}
				if n == active {
					slot := map[int]string{0: "primary", 3: "secondary", 6: "grenade", 7: "perk"}[field]
					if field == 6 && item.Kind != "grenade" {
						slot = ""
					}
					if slot != "" {
						selected[slot] = item.ID
					}
				}
			}
		}
		if _, err := number(values[9], 0, 1); err != nil {
			return nil, err
		}
	}
	return selected, nil
}
func (a *Adapter) checkSkins(body string) error {
	if body == "" {
		return nil
	}
	if len(body) > 192 {
		return errors.New("skin record too large")
	}
	p := strings.Split(body, "^")
	count, e := number(p[0], 1, 12)
	if e != nil || len(p) != count+2 {
		return errors.New("invalid skin count")
	}
	for _, v := range p[1 : count+1] {
		if _, e = number(v, -2147483648, 2147483647); e != nil {
			return e
		}
	}
	_, e = number(p[count+1], 0, count-1)
	return e
}
func (a *Adapter) Write(tx *sql.Tx, account, body string) error {
	if len(body) > 1024 {
		return errors.New("purchase body too large")
	}
	p := strings.Split(body, "|")
	if len(p) != 12 {
		return errors.New("invalid purchase record")
	}
	// The native client echoes its locally calculated reward. Validate syntax,
	// but only finalized server results may change the wallet; stale saves must
	// neither erase rewards nor prevent an otherwise valid equipment update.
	if _, err := number(p[0], 0, 2147483647); err != nil {
		return err
	}
	inv, err := a.Inventory(tx, account)
	if err != nil {
		return err
	}
	expected := a.fields(inv)
	for _, n := range []int{1, 2, 3, 4, 5, 6, 7, 10} {
		if p[n] != expected[n] {
			return errors.New("client cannot change test entitlements")
		}
	}
	selected, err := a.parseEquipment(p[8], inv)
	if err != nil {
		return err
	}
	if err = a.checkSkins(p[11]); err != nil {
		return err
	}
	qty := strings.Split(p[9], "^")
	if len(qty) != 9 {
		return errors.New("invalid bonus quantities")
	}
	for n := range a.Groups["consumables"] {
		_, e := number(qty[n], 0, 2147483647)
		if e != nil {
			return e
		}
		// Quantities come from match authority, never a stale client echo.
	}
	if len(selected) > 0 {
		if err = loadout.SetInTransaction(tx, account, selected); err != nil {
			return err
		}
	} else {
		if _, err = tx.Exec("DELETE FROM loadouts WHERE account=?", account); err != nil {
			return err
		}
	}
	_, err = tx.Exec("INSERT INTO native_equipment(account,equipment,skins) VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET equipment=excluded.equipment,skins=excluded.skins", account, p[8], p[11])
	return err
}
