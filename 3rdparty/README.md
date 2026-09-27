# Vendored third-party headers

Single-header dependencies, checked in and consumed as a `SYSTEM PRIVATE` include
directory so their warnings do not surface in this project's build. There is no
package manager, no submodule, and no `FetchContent` step.

| File | Upstream | Version | License | Modified |
|---|---|---|---|---|
| `CLI11.hpp` | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) | 2.6.2 | BSD-3-Clause (`LICENSE.CLI11`) | no |
| `croncpp.h` | [mariusbancila/croncpp](https://github.com/mariusbancila/croncpp) | base revision unknown | MIT (`LICENSE.croncpp`) | **yes** |

## croncpp

This copy came from `ticketeer-consortium/3rdparty/croncpp.h`, where it carried **no
copyright, no license text, no version, and no upstream identification** across its 1685
lines. Upstream croncpp is MIT, whose only condition is that the copyright and permission
notice travel with the code, so that copy did not satisfy its own license terms. The
notice was restored at the top of `croncpp.h` and duplicated into `LICENSE.croncpp` here.

**The upstream base revision is unknown** and is recorded as unknown rather than guessed.
The vendored file carried no version marker, and it is a genuine fork rather than a
verbatim copy, so it cannot be matched to an upstream tag by inspection alone. Confirm the
notice and pin the base revision against upstream before this file is distributed outside
this repository.

Local modifications present in this copy, each verified against the source:

- optional 7th field (year), with `supports_years` detection and the
  `CRON_MIN_YEARS` / `CRON_MAX_YEARS` traits;
- `enum class day_field_rule { intersect, either, reject }`;
- `has_reachable_date` validation;
- `cron_next_ceil` and `cron_prev`;
- DST-aware `find_next_after`, bounded by `CRON_MAX_DST_SHIFT`.

`YEET_CRONCPP_FORK` is defined by the header so code can detect the fork's extensions;
stock upstream defines no such macro.

## License compatibility

This project is AGPL-3.0. MIT and BSD-3-Clause are both inbound-compatible with it.
