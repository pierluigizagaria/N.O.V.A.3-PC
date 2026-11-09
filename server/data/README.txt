Server mapping data
===================

Keep mapping.json as UTF-8 JSON with two-space indentation. It is server data,
not a client installation: no engine, map or texture files are loaded from it.
The intended installation location is data/mapping.json beside the executable.
The installed database lives at data/backend.db and TLS identity is in
data/certificates/. The whole installation is relocatable; paths are resolved
from the executable, not from a configuration file or the working directory.

The existing JSON format is retained without conversion:
- library, resource: compatibility identity hashes, not paths to client files.
- rows: one record per item, preserving server_id, native coordinates, label,
  minimum_level, required_rank, resource_price and resource_owned.
- policy: existing progression/grant policy. The file-format work does not alter
  ownership, prices, quantities, ordering or gameplay behavior.

Null requirement/ownership fields retain their existing meaning. Native ordinals
are explicit; row reordering does not change those coordinates. Maximum: 1 MiB.
The server validates the mapping against its database catalogue before serving.

Numeric authority definitions live in data/authority.json. This readable JSON
file is separate from implementation code and contains versioned numeric data,
not scripts. The server validates required fields, IDs, reference integrity,
numeric domains and catalogue compatibility before opening services. Loaded
definitions remain immutable; edits take effect only after a server restart.
The current source data is still being extended for complete match authority.

There is no requests.json or price-request.txt dependency. The native price GET
route is part of the server protocol implementation. Its XML response is generated
directly from the mapping. The server neither loads a response template nor makes
an outbound request. Mapping JSON is not the response format expected by the game.

There is no server configuration file. Directory, state, catalogue and mapping
selectors have been removed. The server finds the single database catalogue
compatible with the mapping and validates its rows; ambiguous data is rejected.
Use nova-server --check to validate installed data without starting services or
changing database records. SQLite may use WAL/SHM coordination files.

Account datasets remain under ignored local/. The separately owner-approved
technical collision/spawn resource is embedded from internal/world/maps.bin.gz.
It contains no original code, scripts, graphics or audio. Normal installation
validation checks its contents; no client installation or operator extraction
is required. Interactive map rules and match authority remain incomplete.
