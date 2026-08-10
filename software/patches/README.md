# Vendor library patches

Patches that must be applied to Infineon's ModusToolbox libraries after `make getlibs`.
Those libraries are fetched from Infineon rather than committed here, so their contents are not
part of this repository and any local edits to them would otherwise be lost on a fresh clone.

## `btstack-integration-bless-ipc-featuremask.patch`

**Without this patch the BLE stack does not start.** The firmware compiles fine either way, so
the failure only shows up on hardware: `Cy_BLE_StackSetFeatureConfig` returns
`CY_BLE_ERROR_INVALID_PARAMETER` and the controller never comes up.

`btstack-integration` ships with `CY_BLE_FEATURE_MASK` set to `0x000Bu`, requesting Data Length
Extension, LE Privacy 1.2, and the 2 Mbps PHY. The `cat1cm0p` v1.10.0 prebuilt CM0+ BLESS
controller binary rejects that combination during parameter validation. Reducing the mask to
`0x0001u` (DLE only) makes the controller accept it.

The resolvable, bonded, and whitelist sizes drop alongside it. The resolvable list must be `0`
when Privacy 1.2 is off, and 16 entries each was more than this application needs.

Nothing in this project uses Privacy or the 2 Mbps PHY, so the reduced mask costs nothing here.

### Applying it

From `software/mtb_shared/btstack-integration/release-v4.7.0/`:

```bash
git apply /path/to/software/patches/btstack-integration-bless-ipc-featuremask.patch
```

Or from the repository root:

```bash
git -C software/mtb_shared/btstack-integration/release-v4.7.0 apply \
    "$(pwd)/software/patches/btstack-integration-bless-ipc-featuremask.patch"
```

Check that it took:

```bash
grep CY_BLE_FEATURE_MASK \
  software/mtb_shared/btstack-integration/release-v4.7.0/COMPONENT_BLESS-IPC/platform/ipc/PSoC6IPC.h
```

It should read `0x0001u`. Then rebuild.
