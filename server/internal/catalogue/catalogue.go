// Package catalogue admits immutable, versioned local catalogue descriptions.
package catalogue

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"regexp"
)

var identifier = regexp.MustCompile(`^[a-z][a-z0-9_.-]{0,63}$`)

func ValidID(s string) bool { return identifier.MatchString(s) }

type Item struct {
	ID          string     `json:"id"`
	Kind        string     `json:"kind"`
	Supported   bool       `json:"supported"`
	MaxQuantity int        `json:"max_quantity"`
	Native      *NativeRef `json:"native,omitempty"`
}

// NativeRef preserves original group/position independently of the server ID.
type NativeRef struct {
	Group            string `json:"group"`
	Ordinal          int    `json:"ordinal"`
	TypeID           int    `json:"type_id"`
	ResourceObjectID int    `json:"resource_object_id"`
}

func (i Item) HasSupply() bool {
	return i.Kind == "consumable" || (i.Kind == "grenade" && i.MaxQuantity > 0)
}

type Catalogue struct {
	Version        string `json:"version"`
	Source         string `json:"source"`
	LibrarySHA256  string `json:"library_sha256,omitempty"`
	ResourceSHA256 string `json:"resource_sha256,omitempty"`
	Items          []Item `json:"items"`
}

func Decode(data []byte) (Catalogue, error) {
	var c Catalogue
	if len(data) > 1024*1024 {
		return c, errors.New("catalogue exceeds 1 MiB")
	}
	d := json.NewDecoder(bytes.NewReader(data))
	d.DisallowUnknownFields()
	if err := d.Decode(&c); err != nil {
		return c, fmt.Errorf("catalogue: %w", err)
	}
	if d.Decode(new(any)) != io.EOF {
		return c, errors.New("catalogue has trailing data")
	}
	return c, c.Validate()
}
func (c Catalogue) Validate() error {
	if !ValidID(c.Version) || len(c.Items) == 0 || len(c.Items) > 4096 {
		return errors.New("invalid catalogue version or item count")
	}
	switch c.Source {
	case "synthetic":
		if c.LibrarySHA256 != "" || c.ResourceSHA256 != "" {
			return errors.New("synthetic catalogue cannot claim a library identity")
		}
	case "native":
		if c.LibrarySHA256 != "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e" {
			return errors.New("unsupported native library identity")
		}
		if !regexp.MustCompile(`^[a-f0-9]{64}$`).MatchString(c.ResourceSHA256) {
			return errors.New("native catalogue requires resource SHA256")
		}
	default:
		return errors.New("catalogue source must be synthetic or native")
	}
	seen := map[string]bool{}
	groups := map[string]map[int]bool{}
	kinds := map[string]string{"primary": "weapon", "secondary": "weapon", "addons": "addon", "perks": "perk", "killchains": "killchain", "signatures": "signature", "presets": "preset", "consumables": "consumable"}
	for _, item := range c.Items {
		if !ValidID(item.ID) || seen[item.ID] {
			return errors.New("invalid or duplicate item ID")
		}
		seen[item.ID] = true
		if c.Source == "synthetic" && item.Native != nil {
			return errors.New("synthetic item cannot claim native coordinates")
		}
		if c.Source == "native" {
			r := item.Native
			if r == nil || r.Ordinal < 0 || r.Ordinal > 4095 || r.TypeID < 0 || r.TypeID > 2147483647 || r.ResourceObjectID <= 0 || r.ResourceObjectID > 2147483647 {
				return errors.New("invalid native coordinates")
			}
			if r.Group == "items" {
				if item.Kind != "grenade" && item.Kind != "equipment" {
					return errors.New("incompatible native item kind")
				}
			} else if kinds[r.Group] == "" || kinds[r.Group] != item.Kind {
				return errors.New("incompatible native group/kind")
			}
			if groups[r.Group] == nil {
				groups[r.Group] = map[int]bool{}
			}
			if groups[r.Group][r.Ordinal] {
				return errors.New("duplicate native ordinal")
			}
			groups[r.Group][r.Ordinal] = true
			if item.Kind == "grenade" && item.MaxQuantity != 0 {
				return errors.New("native grenades are equipment, not bonus consumable supplies")
			}
		}
		switch item.Kind {
		case "weapon", "perk", "equipment", "addon", "killchain", "signature", "preset":
			if item.MaxQuantity != 0 {
				return errors.New("non-consumable quantity must be zero")
			}
		case "grenade":
			if c.Source == "native" {
				continue
			}
			fallthrough
		case "consumable":
			if item.MaxQuantity < 1 || item.MaxQuantity > 1000 {
				return errors.New("supply quantity cap must be 1..1000")
			}
		default:
			return errors.New("unsupported item kind")
		}
	}
	for _, ordinals := range groups {
		for i := 0; i < len(ordinals); i++ {
			if !ordinals[i] {
				return errors.New("native ordinals must be contiguous from zero")
			}
		}
	}
	return nil
}
func (c Catalogue) Find(id string) (Item, bool) {
	for _, v := range c.Items {
		if v.ID == id {
			return v, true
		}
	}
	return Item{}, false
}
