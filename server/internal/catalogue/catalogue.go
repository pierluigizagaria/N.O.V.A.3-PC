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
	ID          string `json:"id"`
	Kind        string `json:"kind"`
	Supported   bool   `json:"supported"`
	MaxQuantity int    `json:"max_quantity"`
}
type Catalogue struct {
	Version       string `json:"version"`
	Source        string `json:"source"`
	LibrarySHA256 string `json:"library_sha256,omitempty"`
	Items         []Item `json:"items"`
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
		if c.LibrarySHA256 != "" {
			return errors.New("synthetic catalogue cannot claim a library identity")
		}
	case "native":
		if c.LibrarySHA256 != "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e" {
			return errors.New("unsupported native library identity")
		}
	default:
		return errors.New("catalogue source must be synthetic or native")
	}
	seen := map[string]bool{}
	for _, item := range c.Items {
		if !ValidID(item.ID) || seen[item.ID] {
			return errors.New("invalid or duplicate item ID")
		}
		seen[item.ID] = true
		switch item.Kind {
		case "weapon", "perk":
			if item.MaxQuantity != 0 {
				return errors.New("non-consumable quantity must be zero")
			}
		case "grenade":
			if item.MaxQuantity < 1 || item.MaxQuantity > 1000 {
				return errors.New("grenade quantity cap must be 1..1000")
			}
		default:
			return errors.New("unsupported item kind")
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
