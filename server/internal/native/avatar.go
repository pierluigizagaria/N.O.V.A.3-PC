package native

import (
	"bytes"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"image"
	_ "image/jpeg"
	"image/png"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

const defaultAvatarKey = "local-default"

// A successful empty download leaves the native avatar texture unset. The
// original menu then uses its own default player portrait from client assets;
// no extracted artwork is stored, served or packaged with the server.
func defaultAvatar() string { return "" }

// Keys fit the original 32-byte avatar buffers. Accounts without an upload use
// the original client's default portrait, as requested by the owner.
func (s *Services) portrait(account string) (string, []byte, error) {
	var key string
	var body []byte
	err := s.Store.DB.QueryRow("SELECT key,body FROM account_avatars WHERE account=?", account).Scan(&key, &body)
	if err == nil || !errors.Is(err, sql.ErrNoRows) {
		return key, body, err
	}
	return defaultAvatarKey, nil, nil
}

func (s *Services) avatars(r legacy.Request) (string, error) {
	owner := r.Fields["u"]
	switch r.Function {
	case 71:
		data, err := base64.StdEncoding.Strict().DecodeString(r.Fields["d"])
		if err != nil || len(data) == 0 || len(data) > 3072 {
			return "", errors.New("invalid avatar encoding")
		}
		config, _, err := image.DecodeConfig(bytes.NewReader(data))
		if err != nil || config.Width < 1 || config.Height < 1 || config.Width > 256 || config.Height > 256 {
			return "", errors.New("invalid avatar dimensions")
		}
		source, _, err := image.Decode(bytes.NewReader(data))
		if err != nil {
			return "", err
		}
		// The native portrait is 32x32. Store only normalized pixels, dropping
		// metadata, trailing payloads and any original input representation.
		img := image.NewNRGBA(image.Rect(0, 0, 32, 32))
		for y := 0; y < 32; y++ {
			for x := 0; x < 32; x++ {
				img.Set(x, y, source.At(source.Bounds().Min.X+x*config.Width/32, source.Bounds().Min.Y+y*config.Height/32))
			}
		}
		var normalized bytes.Buffer
		if err = png.Encode(&normalized, img); err != nil {
			return "", err
		}
		// Include the owner so equal pictures cannot collide across accounts.
		hash := sha256.New()
		hash.Write([]byte(owner))
		hash.Write(normalized.Bytes())
		key := "a" + hex.EncodeToString(hash.Sum(nil)[:12])
		_, err = s.Store.DB.Exec("INSERT INTO account_avatars(account,key,body) VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET key=excluded.key,body=excluded.body", owner, key, normalized.Bytes())
		return "", err
	case 102:
		_, err := s.Store.DB.Exec("DELETE FROM account_avatars WHERE account=? AND key=?", owner, r.Fields["d"])
		return "", err
	case 101:
		key := r.Fields["d"]
		if key == defaultAvatarKey {
			return defaultAvatar(), nil
		}
		var body []byte
		if len(key) > 31 || len(key) < 2 {
			return "", errors.New("invalid avatar key")
		}
		if key[0] == 'p' {
			target, err := accounts.FindPublic(s.Store, "", key[1:])
			if err != nil {
				return "", err
			}
			// Retire generated keys from the earlier delivery without breaking
			// native thumbnail requests still in flight during the transition.
			_ = target
			return defaultAvatar(), nil
		} else if err := s.Store.DB.QueryRow("SELECT body FROM account_avatars WHERE key=?", key).Scan(&body); err != nil {
			return "", err
		}
		return "d" + base64.StdEncoding.EncodeToString(body), nil
	case 70, 100:
		target := owner
		if r.Function == 70 && (r.Fields["n"] != "" || r.Fields["un"] != "") {
			var err error
			target, err = accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
			if err != nil {
				return "", err
			}
		}
		key, _, err := s.portrait(target)
		if err != nil {
			return "", err
		}
		if r.Function == 100 {
			// This reader counts pipe separators as records; no trailing pipe.
			return key + "^png^1", nil
		}
		// GS_OnlineLoginPanel always downloads the default-key field (+0xb8),
		// even when the SDK can describe an uploaded-key field (+0xb0). Publish
		// the account's selected portrait in the default record it consumes.
		return fmt.Sprintf("n|0|d|%s|t|png|url|%s|", key, key), nil
	}
	return "", errors.New("unsupported avatar operation")
}
