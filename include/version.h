// Firmware version and where updates come from (SPEC.md 5.7).
// Bump FW_VERSION for every release; tools/make_release.py reads it and the
// GitHub release tag must be "v" + FW_VERSION.
#pragma once

#define FW_VERSION "1.0.4"

// GitHub repository ("owner/name") whose releases carry the updates. It must be
// public: the device downloads without logging in. Empty = update checks off.
#define OTA_REPO "shemunkee/trailer-display-cyd"
