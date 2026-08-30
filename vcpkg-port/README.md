# vcpkg port

A ready-to-submit [vcpkg](https://github.com/microsoft/vcpkg) port for
MemoryPackCpp. It is kept here so the port and the library stay in step; the
files are copied into a vcpkg checkout to publish.

## Using it before it is in the registry

Point vcpkg at this directory as an overlay:

```bash
vcpkg install memorypackcpp --overlay-ports=path/to/MemoryPackCpp/vcpkg-port
```

Or add it to `vcpkg-configuration.json`:

```json
{
  "overlay-ports": [ "./MemoryPackCpp/vcpkg-port" ]
}
```

## Submitting it upstream

1. Tag and publish the release the port refers to (`REF "v${VERSION}"`).
2. Copy `memorypackcpp/` into `ports/` in a vcpkg checkout.
3. Fill in the real hash - `SHA512 0` is a deliberate placeholder:

   ```bash
   vcpkg install memorypackcpp --overlay-ports=./ports
   # the failure message prints the actual SHA512; paste it into portfile.cmake
   ```

4. Verify the port builds on the platforms you claim:

   ```bash
   vcpkg install memorypackcpp:x64-windows memorypackcpp:x64-linux
   ```

5. `vcpkg x-add-version memorypackcpp` and open the PR.

## Keeping it current

On every release, bump `version` in `vcpkg.json` and refresh the `SHA512`. The
port itself should not need other changes - it only installs headers and the
CMake package config, both of which are driven by the root `CMakeLists.txt`.
