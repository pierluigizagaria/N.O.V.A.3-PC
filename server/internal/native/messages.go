package native

import (
	"database/sql"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

const mailboxLimit = 1000

func messageText(text string, limit int, header bool) bool {
	if len(text) > limit || !utf8.ValidString(text) || strings.ContainsAny(text, "\x00\r\n|") {
		return false
	}
	return !header || !strings.ContainsAny(text, "^,")
}

func messageTypes(value string) (map[int]bool, error) {
	out := map[int]bool{}
	if value == "" {
		return out, nil
	}
	for _, part := range strings.Split(value, ",") {
		n, err := strconv.Atoi(part)
		if err != nil || (n != 1 && n != 2 && n != 4 && n != 8 && n != 16) || out[n] {
			return nil, errors.New("invalid message type filter")
		}
		out[n] = true
	}
	return out, nil
}

type messageRow struct {
	id, kind                               int
	title, body, sender, recipient         string
	senderNumber, recipientNumber, created int64
	read                                   bool
}

func (s *Services) messages(r legacy.Request) (string, error) {
	owner := r.Fields["u"]
	if r.Function == 77 {
		target, err := accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
		if err != nil || target == owner {
			return "", errors.New("invalid message recipient")
		}
		kind, err := nativeInteger(r.Fields, "t", 0, 1, 16)
		if err != nil || (kind != 2 && kind != 4 && kind != 8 && kind != 16) {
			return "", errors.New("invalid user message type")
		}
		title, body := r.Fields["l"], r.Fields["o"]
		if !messageText(title, 96, true) || !messageText(body, 2048, false) || (kind != 8 && body == "") {
			return "", errors.New("invalid message text")
		}
		if _, _, err = accounts.PublicIdentity(s.Store, target); err != nil {
			return "", err
		}
		if _, _, err = accounts.PublicIdentity(s.Store, owner); err != nil {
			return "", err
		}
		persist := func() error {
			return s.Store.Write(func(tx *sql.Tx) error {
				var inbox, sent int
				if e := tx.QueryRow("SELECT count(*) FROM messages WHERE recipient=? AND inbox_deleted=0", target).Scan(&inbox); e != nil {
					return e
				}
				if e := tx.QueryRow("SELECT count(*) FROM messages WHERE sender=? AND sent_deleted=0", owner).Scan(&sent); e != nil {
					return e
				}
				if inbox >= mailboxLimit || sent >= mailboxLimit {
					return errors.New("mailbox full")
				}
				result, e := tx.Exec("INSERT INTO messages(sender,recipient,type,title,body,created) VALUES(?,?,?,?,?,?)", owner, target, kind, title, body, time.Now().Unix())
				if e != nil {
					return e
				}
				id, e := result.LastInsertId()
				if e != nil {
					return e
				}
				if id > 0x7fffffff {
					return errors.New("message ID exhausted")
				}
				return nil
			})
		}
		if kind == 8 {
			return "", s.rooms.invite(owner, target, time.Now(), persist)
		}
		return "", persist()
	}
	sent := r.Function >= 91
	ownerColumn, deletedColumn := "recipient", "inbox_deleted"
	if sent {
		ownerColumn, deletedColumn = "sender", "sent_deleted"
	}
	if r.Function == 76 || r.Function == 78 || r.Function == 93 || r.Function == 94 {
		id, err := nativeInteger(r.Fields, "m", 0, 1, 0x7fffffff)
		if err != nil || id == 0 {
			return "", errors.New("invalid message ID")
		}
		var body string
		err = s.Store.Write(func(tx *sql.Tx) error {
			query := " WHERE id=? AND " + ownerColumn + "=? AND " + deletedColumn + "=0"
			if e := tx.QueryRow("SELECT body FROM messages"+query, id, owner).Scan(&body); e != nil {
				return e
			}
			if r.Function == 78 || r.Function == 94 {
				_, e := tx.Exec("UPDATE messages SET "+deletedColumn+"=1"+query, id, owner)
				if e != nil {
					return e
				}
				_, e = tx.Exec("DELETE FROM messages WHERE id=? AND inbox_deleted=1 AND sent_deleted=1", id)
				body = ""
				return e
			}
			if !sent {
				_, e := tx.Exec("UPDATE messages SET is_read=1"+query, id, owner)
				return e
			}
			return nil
		})
		return body, err
	}
	rows, err := s.Store.DB.Query("SELECT m.id,m.type,m.title,m.body,a.name,b.name,ai.number,bi.number,m.created,m.is_read FROM messages m JOIN accounts a ON a.id=m.sender JOIN accounts b ON b.id=m.recipient JOIN lobby_identities ai ON ai.account=a.id JOIN lobby_identities bi ON bi.account=b.id WHERE m."+ownerColumn+"=? AND m."+deletedColumn+"=0 ORDER BY m.id DESC LIMIT ?", owner, mailboxLimit)
	if err != nil {
		return "", err
	}
	defer rows.Close()
	var all []messageRow
	for rows.Next() {
		var v messageRow
		if err = rows.Scan(&v.id, &v.kind, &v.title, &v.body, &v.sender, &v.recipient, &v.senderNumber, &v.recipientNumber, &v.created, &v.read); err != nil {
			return "", err
		}
		all = append(all, v)
	}
	if err = rows.Err(); err != nil {
		return "", err
	}
	if r.Function == 91 {
		return strconv.Itoa(len(all)) + "|", nil
	}
	types, err := messageTypes(r.Fields["t"])
	if err != nil {
		return "", err
	}
	state, err := nativeInteger(r.Fields, "r", 2, 0, 2)
	if err != nil {
		return "", err
	}
	var selected []messageRow
	var counts [6][3]int
	for _, m := range all {
		if len(types) != 0 && !types[m.kind] {
			continue
		}
		read := 0
		if m.read {
			read = 1
		}
		if state != 2 && state != read {
			continue
		}
		selected = append(selected, m)
		for i, kind := range []int{1, 2, 4, 8, 16, 32} {
			if kind == 32 || kind == m.kind {
				counts[i][read]++
				counts[i][2]++
			}
		}
	}
	var body strings.Builder
	if r.Function == 74 {
		for i, kind := range []int{1, 2, 4, 8, 16, 32} {
			fmt.Fprintf(&body, "%d^%d^%d^%d|", kind, counts[i][0], counts[i][1], counts[i][2])
		}
		return body.String(), nil
	}
	start, err := nativeInteger(r.Fields, "a", 0, 0, mailboxLimit)
	if err != nil {
		return "", err
	}
	count, err := nativeInteger(r.Fields, "c", 20, 0, 100)
	if err != nil {
		return "", err
	}
	if count == 0 {
		count = 100
	}
	if start > len(selected) {
		start = len(selected)
	}
	end := min(len(selected), start+count)
	fmt.Fprintf(&body, "c|%d|m|", end-start)
	for _, m := range selected[start:end] {
		created := time.Unix(m.created, 0).UTC().Format("2006-01-02 15:04:05")
		if sent {
			fmt.Fprintf(&body, "%d^%s^%d^%s^%s|", m.id, m.title, m.recipientNumber, m.recipient, created)
		} else {
			read := 0
			if m.read {
				read = 1
			}
			fmt.Fprintf(&body, "%d^%d^%s^%s^%d^%s^%d|", m.id, m.kind, m.title, m.sender, m.senderNumber, created, read)
		}
	}
	return body.String(), nil
}
