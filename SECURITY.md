# Security Policy

Thank you for taking the time to help keep **QuicX** and its users safe.
This document describes which versions are eligible for security fixes,
how to report a vulnerability, and what to expect once you do.

> ⚠️ **Pre-1.0 status.**  QuicX is on the `0.x` series and is intended for
> evaluation, embedding into experimental products, and contribution.
> It is **not** marketed as an enterprise-grade, production-SLA product.
> Security reports are nevertheless taken seriously and patched on a
> best-effort basis under the timeline below.

---

## Supported versions

During the `0.x` series only the most recent minor release receives security
patches.  Older `0.y` releases are unsupported once `0.(y+1).0` ships.

| Version  | Supported          | Notes                                          |
|----------|--------------------|------------------------------------------------|
| `0.1.x`  | ✅ Yes (current)   | Receives security fixes as `0.1.(x+1)` patches |
| `< 0.1`  | ❌ No              | Pre-release; please upgrade                    |

After `1.0.0` ships, this matrix will be widened to cover at least the two
most recent minor lines, in line with semantic-versioning expectations.

---

## What counts as a security issue?

Examples of issues we treat as **security-sensitive** (please use private
disclosure):

- Remote denial-of-service: any input from a peer that causes excessive
  CPU, memory, or socket consumption.  *Concrete example fixed in 0.1.0:*
  a malformed unknown HTTP/3 frame split across two `OnData` calls could
  drive the decoder into a state where it expected ~700 MB of skip data,
  effectively hanging the connection.
- Memory-safety bugs reachable from a network peer: out-of-bounds reads or
  writes, use-after-free, double-free, type confusion, uninitialised reads.
- Cryptographic weaknesses: nonce reuse, key-derivation mistakes, padding
  oracles, downgrade attacks against TLS 1.3 / QUIC version negotiation.
- Authentication / connection-ID confusion that could let a peer hijack or
  blind-inject into another connection.
- Path-traversal or unbounded-resource issues in any code that processes
  untrusted bytes (HTTP/3 routing, QPACK, frames, packet-number recovery,
  etc.).

Examples of issues that are **not** security-sensitive and can go through
the normal public issue tracker:

- Build failures on a specific compiler.
- Performance regressions that don't degrade availability.
- Spec-conformance gaps that don't affect safety (please still file them).
- Issues that require local privileged access already capable of causing
  the same harm (e.g. ptrace, root-only filesystem access).

When in doubt, **err on the side of private disclosure**; we will tell you
quickly if the report can be made public.

---

## How to report a vulnerability

We support two private channels.  Please use **either one** — duplicating
across both is unnecessary.

### 1. GitHub Security Advisory (preferred, once the project is on GitHub)

Once the QuicX repository is published on GitHub, the preferred reporting
channel is the repository's
*Security → Report a vulnerability* page, which creates a private
**GitHub Security Advisory** that only maintainers can read.  This gives
you and us a private workspace to discuss the issue, propose a fix, and
coordinate disclosure.

### 2. Encrypted email

If GitHub is not appropriate (e.g. the project's GitHub home is not yet
live, or you prefer email), send the report to:

```
security@quicx.invalid
```

> 📌 Maintainer note: replace the address above with the real intake mailbox
> at GA time, and publish the corresponding PGP key under
> `docs/security/public-key.asc`.  Until then, file the report through the
> private GitHub Security Advisory channel above.

Please include:

- A description of the issue and its expected impact.
- A minimal reproducer if you have one.  For network-protocol bugs a
  packet capture (`.pcap` / `qlog` trace) is extremely helpful.
- The QuicX commit hash / release tag you reproduced against.
- Your preferred disclosure timeline and whether you want public credit.

**Please do not** open a public GitHub issue, post the details on a
mailing list, tweet a teaser, or commit a public PoC until we have agreed
on a disclosure schedule.

---

## What to expect after you report

| Stage                     | Target turnaround                  |
|---------------------------|------------------------------------|
| Acknowledgement of receipt | within **3 business days**         |
| Triage decision (accept / dispute / duplicate) | within **10 business days** |
| Fix or mitigation in `main` | within **30 days** for typical issues; longer for protocol-design defects requiring spec discussion |
| Public advisory + patched release | coordinated with the reporter (default: when the patched release ships) |

These are **best-effort targets** for an open-source project, not a
contractual SLA.  Genuinely time-critical issues (active exploitation,
trivial remote crash) will be prioritised above the table.

We will keep you in the loop at each stage, credit you in the advisory and
`CHANGELOG.md` (unless you ask to remain anonymous), and — where the fix is
non-obvious — invite you to review the patch before public release.

---

## Disclosure model

QuicX follows a **coordinated-disclosure** model:

1. The reporter contacts us privately via one of the channels above.
2. We confirm the vulnerability and develop a fix.
3. A patched release is prepared (usually a `0.y.(z+1)` patch).
4. The patch is published; **simultaneously**, a public advisory is
   issued describing the issue, affected versions, mitigations, and the
   fix.
5. The reporter is credited in the advisory and `CHANGELOG.md`.

If at any point the reporter prefers a different schedule, we will try to
accommodate it.  We **do not** offer a bug-bounty program at this time.

---

## Hardening recommendations for embedders

These are not vulnerabilities in QuicX, but we strongly recommend the
following for anyone embedding the library into a product:

- Enable **AddressSanitizer** and **UndefinedBehaviorSanitizer** in your
  CI builds.  QuicX's own CI runs ASan + UBSan + TSan and we expect
  downstream users to do the same on integration tests.
- Set a sensible upper bound on `Http3ServerConfig` / `QuicConfig`
  flow-control windows, max concurrent streams, and connection-idle
  timeout to limit per-connection resource usage.
- Use the built-in metrics registry (`MetricsRegistry`) to monitor
  unusual rates of dropped packets, RESET frames, or migration events,
  which can indicate an attempted attack.
- Keep the BoringSSL submodule under `third/boringssl` up to date — QuicX
  inherits its TLS hardening directly from BoringSSL.

---

## Scope notes

This policy covers the code in this repository: `src/`, `example/`,
`test/`, the build configuration, and any officially published artefacts.

Vulnerabilities in **upstream dependencies** (BoringSSL, GoogleTest,
Google Benchmark) should be reported upstream first; once a fix exists
there, please open a tracking issue here so we can pin to the patched
version.

Thank you for helping keep QuicX users safe.
