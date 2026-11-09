// Package legacy implements the original text envelope and low-bit-first request codec.
package legacy

import (
	"errors"
	"strconv"
	"strings"
)

const alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
const maxRequest = 16384

func Encode(data []byte) string {
	var out strings.Builder
	var accumulator uint32
	bits := 0
	for _, b := range data {
		accumulator |= uint32(b) << bits
		bits += 8
		for bits >= 6 {
			out.WriteByte(alphabet[accumulator&63])
			accumulator >>= 6
			bits -= 6
		}
	}
	if len(data) > 0 {
		out.WriteByte(alphabet[accumulator&63])
	}
	return out.String()
}

func Decode(text string) ([]byte, error) {
	if len(text) == 0 || len(text) > maxRequest*2 {
		return nil, errors.New("invalid encoded request length")
	}
	out := make([]byte, 0, len(text)*6/8)
	var accumulator uint32
	bits := 0
	for _, b := range []byte(text) {
		value := strings.IndexByte(alphabet, b)
		if value < 0 {
			return nil, errors.New("invalid request symbol")
		}
		accumulator |= uint32(value) << bits
		bits += 6
		if bits >= 8 {
			out = append(out, byte(accumulator))
			accumulator >>= 8
			bits -= 8
		}
	}
	if len(out) > maxRequest || Encode(out) != text {
		return nil, errors.New("noncanonical or oversized request")
	}
	return out, nil
}

type Request struct {
	Function int
	Fields   map[string]string
	Prefix   string
}

func Parse(data []byte) (Request, error) {
	r := Request{Fields: make(map[string]string)}
	if len(data) == 0 || len(data) > maxRequest || strings.ContainsAny(string(data), "\x00\r\n") {
		return r, errors.New("invalid native request")
	}
	parts := strings.Split(strings.TrimSuffix(string(data), "|"), "|")
	if len(parts) < 2 || len(parts)%2 != 0 || (parts[0] != "f" && parts[0] != "g") {
		return r, errors.New("invalid native field pairs")
	}
	function, err := strconv.Atoi(parts[1])
	if parts[0] == "g" {
		function += 500
	}
	if err != nil || function < 1 || function > 4096 {
		return r, errors.New("invalid native function")
	}
	r.Function = function
	r.Prefix = parts[0]
	for n := 2; n < len(parts); n += 2 {
		if parts[n] == "" || len(parts[n]) > 32 || len(parts[n+1]) > 4095 {
			return r, errors.New("invalid native field")
		}
		if _, duplicate := r.Fields[parts[n]]; duplicate {
			return r, errors.New("duplicate native field")
		}
		r.Fields[parts[n]] = parts[n+1]
	}
	return r, nil
}

func (r Request) Success(body string) string {
	if r.Prefix == "g" {
		return "g|" + strconv.Itoa(r.Function-500) + "|r|s|" + body
	}
	return Success(r.Function, body)
}

func (r Request) Failure(code int) string {
	if r.Prefix == "g" {
		return "g|" + strconv.Itoa(r.Function-500) + "|r|e|" + strconv.Itoa(code) + "|"
	}
	return Failure(r.Function, code)
}

func Success(function int, body string) string {
	return "f|" + strconv.Itoa(function) + "|r|s|" + body
}

func Failure(function, code int) string {
	return "f|" + strconv.Itoa(function) + "|r|e|" + strconv.Itoa(code) + "|"
}
