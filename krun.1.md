crun 1 "User Commands"
==================================================

# NAME

krun - crun based OCI runtime using libkrun to run containerized programs in
isolated KVM environments

# SYNOPSIS

krun [global options] command [command options] [arguments...]

# DESCRIPTION

krun is a sub package of the crun command line program for running Linux
containers that follow the Open Container Initiative (OCI) format. The krun
command is a symbolic link to the crun executable, that tells crun to run in
krun mode.

krun uses the dynamic libkrun library to run processes in an isolated
environment using KVM Virtualization.

libkrun integrates a VMM (Virtual Machine Monitor, the userspace side of a
Hypervisor) with the minimum amount of emulated devices required for its
purpose, abstracting most of the complexity from Virtual Machine management.

Because of the additional isolation, sharing content with processes and other
containers outside of the krun VM is more difficult.

# CONFIGURATION

The microVM can be configured through OCI annotations or a
**.krun_vm.json** file placed at the root of the container image.
When both are present, OCI annotations take precedence.

## OCI Annotations

OCI annotations can be passed at container creation time. For
example, with podman:

    podman run --runtime=krun --annotation krun.nested_virt=1 ...

The following annotations are supported:

**krun.cpus**=*NUM*
:   Number of vCPUs for the microVM (maximum 16). If not set, defaults
    to the number of CPUs available via the process CPU affinity.

**krun.ram_mib**=*NUM*
:   Amount of RAM in MiB for the microVM. Values below 128 MiB are
    ignored. If not set, defaults to the OCI memory limit if present,
    otherwise 1024 MiB.

**krun.gpu_flags**=*FLAGS*
:   Enable virtio-gpu with the specified virgl flags. Requires
    **/dev/dri** and **/usr/libexec/virgl_render_server** to be
    available.

**krun.use_passt**=*NUM*
:   When set to a value greater than 0, enable passt-based networking
    in the microVM.

**krun.nested_virt**=*NUM*
:   When set to a value greater than 0, enable nested virtualization
    in the microVM, exposing hardware virtualization support (VMX on
    Intel, SVM on AMD) to the guest. This requires nested
    virtualization to be enabled on the host (e.g.
    **/sys/module/kvm_intel/parameters/nested** or
    **/sys/module/kvm_amd/parameters/nested** must report **Y** or
    **1**). A warning is emitted if the host does not appear to
    support nested virtualization.

**krun.variant**=*VARIANT*
:   Select an alternative libkrun variant. Supported values are
    **sev** (AMD SEV confidential workloads) and **aws-nitro** (AWS
    Nitro Enclaves).

## VM Configuration File

A **.krun_vm.json** file can be placed at the root of the container
image to provide default VM settings. The file is a JSON object with
the following optional fields:

- **cpus** (integer): same as the **krun.cpus** annotation.
- **ram_mib** (integer): same as the **krun.ram_mib** annotation.
- **gpu_flags** (integer): same as the **krun.gpu_flags** annotation.
- **use_passt** (integer): same as the **krun.use_passt** annotation.
- **nested_virt** (integer): same as the **krun.nested_virt** annotation.
- **flavor** (string): same as the **krun.variant** annotation.
- **kernel_path** (string): path to an external kernel.
- **kernel_format** (integer): kernel format identifier.
- **initrd_path** (string): path to an initrd image.
- **kernel_cmdline** (string): kernel command line.
- **virtiofs_tag** (string): VirtioFS tag (defaults to **/dev/root**).
- **virtiofs_shm_size** (integer): VirtioFS DAX shared memory size in
  bytes (defaults to 512 MiB).

Example:

    {"nested_virt": 1, "cpus": 4, "ram_mib": 2048}

## DISK CONFIGURATION

krun can attach additional raw disk images to the libkrun microVM as
virtio-block data disks.  Disk configuration is additive: it does not replace
the root disk, change virtiofs, mount filesystems inside the guest, or support
hotplug after the VM has started.

Disk annotations use a contiguous zero-based index:

```
krun.disk.0.path=/path/to/nix.raw
krun.disk.0.id=nix
krun.disk.0.readonly=false
```

The supported fields are:

- `krun.disk.N.path`: path to the raw disk image.  This field is required.
- `krun.disk.N.id`: block device identifier passed to libkrun.  This field is
  required.
- `krun.disk.N.readonly`: optional read-only flag.  The default is `false`.

The disk image path must point to an existing non-empty regular file.  When
`readonly` is `false`, crun must be able to open the image read-write; use
`readonly=true` for images that should only be opened read-only.

`N` must be `0` or a canonical decimal number without a sign or leading zeroes.
Indexes must be contiguous from `0`.  The annotation `readonly` value must be
exactly `true` or `false`.

The equivalent `.krun_vm.json` form is:

```json
{
  "disks": [
    {
      "path": "/path/to/nix.raw",
      "id": "nix",
      "readonly": false
    }
  ]
}
```

If any `krun.disk.N.*` annotation is present, the annotation disk list overrides
the `.krun_vm.json` `disks` list entirely.  Other top-level `.krun_vm.json`
settings keep their existing behavior.

Only raw disk images are supported through this interface.  The libkrun
`format`, `direct_io`, and `sync_mode` options are intentionally not exposed.
Extra disks are rejected for the `sev` flavor because that path configures a
root disk with `krun_set_root_disk`, which is mutually exclusive with
`krun_add_disk`.
Unsupported fields in the `krun.disk.N.*` annotation namespace or inside
`.krun_vm.json` `disks` entries are rejected.

# COMMANDS

See crun.1 man page for the commands available to krun

# SEE ALSO
crun.1
