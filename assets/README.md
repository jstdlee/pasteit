Offline renderer assets
=======================

`mermaid.min.js` is the complete browser bundle from Mermaid 12.0.0:
https://cdn.jsdelivr.net/npm/mermaid@12.0.0/dist/mermaid.min.js

SHA-256: `28fca7ae6ebc7ed7bb63bde63136a74bfef14f296a57e403657eeb8b32836073`

Mermaid is licensed under MIT; see `MERMAID_LICENSE`. The bundle retains its
embedded third-party license notices. At build time the file is copied beside
the executable, and at render time it is embedded in each exported HTML file.

Native QR codes use Project Nayuki's C++ QR Code generator, pinned to commit
`3c6d0b3cefb4e049dc337e82237c9644399716a8` in `third_party/qrcodegen`.
Its MIT license is included in the source headers. PNG output uses stb_image_write,
pinned to commit `5736b15f7ea0ffb08dd38af21067c314d6a3aae9` in
`third_party/stb`; its MIT/public-domain license is included in the header.
