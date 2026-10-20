# Product-version build override

Configure the Darling superproject with
`-DDARLING_EMULATED_OS_PRODUCT_VERSION=26.5` to change the string returned by
`kern.osproductversion`, then rebuild and install libsystem_kernel. The default
remains `26.0`. This is a CMake cache setting, not a runtime environment variable;
reconfiguring without the option preserves a previous override. Use
`-UDARLING_EMULATED_OS_PRODUCT_VERSION` to restore the default.

The option supports reproducible version-gating experiments without editing
the source or changing the default for other builders. It does not implement
the APIs of the advertised macOS release. It also does not update the parent
repository's `SystemVersion.plist`, Darwin kernel release/version, or build
identifier. For a coherent installed identity, coordinate those separately;
otherwise applications using different version sources can disagree.

Run `ruby darling/tests/product-version.rb` with Ruby, CMake and a native C/C++
compiler installed. The probe uses the actual CMake preamble and compiles
normal/dyld-configured executables to check defaults, overrides, cache
persistence and reset. This is not a full Darling configure or guest sysctl
test and does not claim compatibility with the selected release.
