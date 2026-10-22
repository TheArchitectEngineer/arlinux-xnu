# UniXNU reuse review

Reviewed source on 2026-10-22: [UniXNU](https://github.com/UniXNU/UniXNU/tree/82dd277f51d5e5bb6430a51c13c3f8e5fafc5505)
and its declared [NativeMetal revision](https://github.com/UniXNU/NativeMetal/tree/ff0c70d7249a04ee381d9854c214069324a34d5c).
Both were cloned for source inspection only; no build, GPU execution or integration
is claimed. The temporary source views live under ignored build/reference-sources.

UniXNU boots XNU through UEFI on non-Apple ARM64 hardware. Its
[graphics documentation](https://github.com/UniXNU/UniXNU/blob/82dd277f51d5e5bb6430a51c13c3f8e5fafc5505/docs/drivers/native-adreno.md)
reports GPU system composition and per-process Metal devices on sheng/Adreno 740.
Those are upstream results, not Linux/Android compatibility evidence here.

| Source boundary | Potential reuse | Work still required here |
| --- | --- | --- |
| NativeBridgeMetalFrontend.mm and bridge/NativeMetalObjects.h | Objective-C Metal objects over opaque C ABI runtime handles, including resources and encoders | Match the pinned iOS 16 framework ABI, ownership and error behavior; cross the Darwin/host TLS boundary safely |
| bridge/NativeMetalObjects.rs and NativeMetalBridge.rs | metal2vulkan runtime integration, AIR translation and pipeline/resource operations | Host Vulkan loader/device integration, supported shader behavior and actual app output validation |
| IOSurface texture/import paths and frontend checks | Concrete examples of shared storage, render/readback and surface sampling | Our Linux memory/IOSurface lifetime and synchronization; no assumption that an XNU surface is a Linux DMA-BUF |
| NativeAdrenoMetalPlugin.mm / MTLAddDevice registration | Framework device discovery and private protocol requirements | Confirm on the exact iOS 16.7.16 binary before using; upstream discovery alone does not implement our renderer |
| NativeAdreno, NativeMobileDisplay, SMMU/UEFI and 4 KiB kernel patches | Hardware/platform implementation reference | Linux/Android already own GPU, display and memory translation; use their APIs rather than importing XNU hardware ownership |

The practical candidate is the frontend/object bridge plus translator integration,
with host platform calls adapted behind explicit interfaces. It is not necessary
to import a full XNU boot stack to use that work. Our existing software display
and original-app journeys remain the baseline until actual GPU effects pass.

No direct repair for the current Things workloop failure was found in this
review. Its EACCES comes from our Linux nice mapping, while UniXNU uses XNU's
scheduler. Continue that reusable scheduling repair rather than switching the
runtime architecture or importing unrelated graphics code to address it.

UniXNU's main repository is BSD-2-Clause-Patent; NativeMetal is MIT and individual
bridge files carry MIT notices. Check licenses of every selected dependency and
retain original notices if code is imported. Attribution here does not imply
that any source has already been copied into production.
