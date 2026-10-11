# Third-party material

- **Monocypher 4.0.2** (`third_party/monocypher/`), by Loup Vaillant.
  Dual-licensed BSD-2-Clause / CC0-1.0; see `third_party/monocypher/LICENCE.md`.
  Used for Argon2id and XChaCha20-Poly1305 in the app lock.
- **Talon protocol fixtures** (`tests/data/*_v1.json`), copied unmodified from
  <https://github.com/thefalconry/talon> (`protocol/fixtures/`), Apache-2.0.
  They are the canonical wire samples the tests replay. They are test data
  shipped alongside the program, not part of it: they keep their own licence
  and are not relicensed under the GPL. (Apache-2.0 and GPL-2.0-only are not
  compatible for a combined work, which these files are not; drop the
  directory and the fixture tests if that aggregation is unwanted.)
- **Qt 6**, © The Qt Company. The modules Talos uses are offered under
  LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only; Talos takes them under
  GPL-2.0-only. Not included in this repository.
