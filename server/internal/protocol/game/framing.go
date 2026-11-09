package game

// Native 1.0.7 wire record sizes, including the type byte. Zero is invalid;
// variable records include a type byte and a BE16 total length. These are
// protocol constants, not simulation parameters. Structural recognition does
// not authorize a client command: Session separately admits each operation.
const variableRecord = 0xffffff

var recordSizes = [...]int{
	0, 5, variableRecord, variableRecord, 1, variableRecord, 2, 6, 9, 2,
	1, 1, 2, 9, variableRecord, 1, 18, 1, 17, 44,
	10, 7, 4, 5, variableRecord, 2, 26, 30, 2, 8,
	2, 10, 2, 12, 6, 6, 2, 7, 1, 23,
	6, 19, 3, 4, 2, 2, 1, 4, 8, 3,
	15, 2, 2, 6, 2, 6, 781, variableRecord, 3, 1,
	1, 2, 13, 3, 1026, 9, 257, 3, 3, 6,
	3, 17, 15, 11, 9, 7, 4, 3, 11, 12,
	5, 6, 5, 5, 11, 4, 4, 4, 12, 19,
	18, variableRecord, 13, 14, 4, 5, 3, 4, 16, 18,
	28, 2, 4, 2, 3, 3, 7, 8, 4, 12,
	3, 10, 6, 2, 5, 4, 2, variableRecord, 8, 20,
	3, 3, 8, 8, 2, 6, 6, 6, 6, 2,
	7, variableRecord, 5, 1, 1, 11, 2, 2, 2, 3,
	3, 3, 3, 10, 15, 2, 3, 14, 7, 6,
	2, 2, 2, 9, 14, 7, 5, 25, 2, 3,
	3, 3, 3, 3, 6,
}

// RecordSize returns zero for an unknown type and variableRecord for a
// length-prefixed record. All original message IDs 1..164 are covered.
func RecordSize(kind byte) int {
	if int(kind) >= len(recordSizes) {
		return 0
	}
	return recordSizes[kind]
}
