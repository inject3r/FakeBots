# Third-party code

| Component | Where | License / origin |
| --- | --- | --- |
| RakNet 2.52 (open.mp fork, client-mode patches) | `third_party/raknet/` | (c) 2002-2005 Kevin Jenkins / Jenkins Software, "RakNet" licence, see `third_party/raknet/readme.txt` and `SpeexLicense.txt`. Fork: <https://github.com/openmultiplayer/RakNet>, commit in `UPSTREAM_COMMIT.txt`. Our changes: `third_party/raknet/FAKEBOTS_PATCHES.md`. |
| sampgdk | `lib/sampgdk/` | Apache-2.0, (c) 2011-2015 Zeex |
| SA-MP plugin SDK headers | `lib/sdk/` | SA-MP team plugin SDK |
| nlohmann/json | `lib/json/` | MIT |

## Reproducible builds

Nothing is downloaded at configure time any more: RakNet is part of the source tree, and the
protocol tables are generated from a reviewed upstream commit plus a SA-MP server binary with
`tools/gen_crypt_tables.py` (the result, `SampCryptData.inc`, is committed).

## Test servers

`omp/` and `samp/` are **test fixtures**, not part of the plugin and not part of any release
package:

* `omp/` holds the unmodified open.mp server (MPL-2.0): see `omp/README-SERVER.txt`.
* `samp/` is prepared for the SA-MP 0.3.7 server but does **not** contain `samp03svr`: the SA-MP
  licence forbids redistributing the server package. `samp/get_samp_server.sh` fetches it from the
  official host. See `samp/README-SERVER.txt`.

## Runtime model

Each bot owns one `RakNet::RakClientInterface` and joins the server it runs inside of over UDP,
as an ordinary player. There is no `ConnectNPC()`, no npc-mode process, no server memory hook and
no NPC slot.
