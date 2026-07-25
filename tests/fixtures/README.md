# Upstream test fixtures

Both files are copied byte-for-byte from
`https://github.com/pocket-stack/pocketjs` commit
`e8b7cd83071e4f592bc919ccf4246feb80d68f9e`, which is an ancestor of the
pinned base commit `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`.

| Published path | Upstream path | SHA-256 |
| --- | --- | --- |
| `tests/fixtures/youtube-golden.pkst` | `tests/fixtures/youtube-golden.pkst` | `94a01de4e41c8d86d66382eadbfc19dd1063b4ebafb02ba32786e25fc43d91e5` |
| `tests/fixtures/packages/synthetic.pocket` | `tests/fixtures/packages/synthetic.pocket` | `c87c67a0050b3cd1ed67dc11758d95565eaeedcb2b1769a2a43a9f0e1964eb14` |

They were introduced by upstream commit
`e8b7cd83071e4f592bc919ccf4246feb80d68f9e` and are required by the
vendored Core unit tests.
