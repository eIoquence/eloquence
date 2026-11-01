# Eloquence Hardware Ownership Research

**Status:** Initial research specification  
**Version:** 0.1  
**Scope:** x86-64 functional prototype in QEMU

## Motivation

Conventional kernels mediate access to devices through kernel-owned drivers and APIs. An exokernel offers a useful setting for studying a different boundary: the kernel protects and multiplexes low-level resources while higher-level policy can live outside the kernel.

Eloquence will study the resource boundary itself. Its initial focus is how hardware resources are identified, granted, owned, revoked, and released, and what checks and costs that lifecycle introduces.

This specification defines a research direction, not a claim that the design is novel or already implemented. Relevant prior systems and mechanisms must be surveyed before making a contribution claim.

## Research question

How can an exokernel expose hardware resources through explicit ownership and revocable capabilities while preserving isolation and keeping mediation costs measurable?

### Initial hypothesis

A single, explicit lifecycle for hardware resources can make authorization and revocation behavior testable across device resources, while allowing the cost of those transitions to be measured independently.

The first prototype will test this hypothesis on emulated PCI resources. It will not attempt to establish the hypothesis for every hardware class.

## Resource lifecycle

The working lifecycle is:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

- **Discovery:** identify a resource and its platform-defined bounds.
- **Authorization:** decide whether a protection domain may receive specified rights.
- **Allocation:** reserve the resource or resource range for an owner.
- **Ownership:** permit operations covered by the granted rights.
- **Revocation:** stop further operations through the grant and invalidate derived access as defined by the design.
- **Release:** return the resource to an unowned pool or its platform owner.

Transitions, rights, and failure cases must be explicit. The capability representation, delegation rules, and exact revocation guarantees remain design questions for a later ADR.

## v0.1 scope

### Included

1. x86-64 boot and diagnostic output in QEMU.
2. Inspection of bootloader-provided memory and platform information.
3. A bootstrap allocator for 4 KiB pages from usable physical memory.
4. A documented capability and ownership model for device resources.
5. PCI device discovery in the emulator configuration.
6. A prototype for granting and revoking access to a bounded MMIO range.
7. Functional checks for authorized access, denied access, and post-revocation behavior.
8. Reproducible experiment records, including emulator and build configuration.

### Deferred

- DMA buffers, IOMMU domains, and device isolation claims involving DMA.
- Physical PCIe hardware and performance conclusions about real devices.
- AArch64, RISC-V, and MIPS ports; these are now tracked in the [multi-architecture research plan](multi-architecture-and-fpga.md).
- FPGA peripherals, custom processors, networking, filesystems, and a general-purpose userland.
- A general capability framework for every kernel resource.

These items depend on a working baseline and evidence that the initial lifecycle is useful.

## First experimental slice

The first end-to-end device experiment will use a QEMU-emulated PCI device with an MMIO region.

The PCI scanner is read-only: it discovers functions and decodes BAR fields. A separate targeted transaction probes BAR0 size only on QEMU's educational PCI device, with I/O and memory decoding disabled and original configuration restored. The current source adds a ring-3 demonstration and scalar syscall path. Kernel-owned state records the current domain; syscalls accept neither a caller-supplied actor ID nor a user pointer. The demo attempts owner access, foreign-domain denial, read-only denial, revoke, stale-handle denial, and audit recording. The ELF has not been boot-validated since this change, so these remain intended runtime scenarios rather than observed results. Handles are still predictable slot/generation values and are not an unforgeability guarantee. The Limine HHDM omits ordinary reserved memory in the requested base revision, so the mapper creates a separate virtual mapping and rejects ranges that overlap HHDM-mapped memory.

The prototype should capture:

- resource identity and bounds discovered from the emulated platform;
- the domain receiving a grant and the rights attached to it;
- whether each attempted access is authorized;
- the observable effect of revoking a grant;
- time spent in grant, access mediation, and revoke operations once a stable timing source exists.

The early kernel installs handlers for exception vectors 0–31. They report the exception vector over COM1 and include CR2 for page faults. A fault reported from ring 3 returns to the kernel continuation and terminates that demo domain; kernel-mode exceptions halt. This path has not been boot-validated, and external interrupts remain disabled.

The experiment should include a direct or minimally mediated baseline where that comparison is meaningful. It must report the precise operation being compared; unlike operations should not be combined into a single overhead number.

## Evaluation method

Functional experiments should record the QEMU machine, device configuration, kernel revision, compiler and flags, and expected versus observed behavior. Repeat timing measurements only after the operations and timer source are specified; report distributions and run conditions rather than selecting arbitrary performance targets in advance.

QEMU can support controlled functional experiments. Its timing results do not establish latency or throughput on physical hardware. Any physical-hardware performance claim requires later measurements on named hardware with the configuration recorded.

## Open design questions

- What is the smallest resource unit the kernel can safely grant: a BAR, a subrange, or a device-specific object?
- Which rights apply to MMIO, and how are read/write widths and alignment constrained?
- What state makes a capability unforgeable and prevents unauthorized derivation?
- Does revocation remove mappings synchronously, and how are in-flight accesses handled?
- How should interrupts attach to a device grant and be revoked?
- Which parts of the lifecycle can be architecture-neutral without hiding important hardware differences?

These questions should be answered in design records and experiments as implementation proceeds.
