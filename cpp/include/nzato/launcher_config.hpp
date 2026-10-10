#pragma once

#ifndef NZATO_LAUNCHER_CHINESE
#error "NZATO_LAUNCHER_CHINESE must be defined by the launcher target"
#endif

#ifndef NZATO_LAUNCHER_DEBUG
#error "NZATO_LAUNCHER_DEBUG must be defined by the launcher target"
#endif

namespace nzato {
inline constexpr bool launcher_chinese = NZATO_LAUNCHER_CHINESE != 0;
inline constexpr bool launcher_debug = NZATO_LAUNCHER_DEBUG != 0;
inline constexpr const char* launcher_pack_name = launcher_chinese ? "ddlc-zh.nzp" : "ddlc.nzp";
inline constexpr const char* launcher_pack_name_ndless =
    launcher_chinese ? "ddlc-zh.nzp.tns" : "ddlc.nzp.tns";
} // namespace nzato
