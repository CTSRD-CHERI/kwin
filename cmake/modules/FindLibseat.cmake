#.rst:
# FindLibseat
# -------
#
# Try to find libseat on a Unix system.
#
# This will define the following variables:
#
# ``Libseat_FOUND``
#     True if (the requested version of) libseat is available
# ``Libseat_VERSION``
#     The version of libseat

#=============================================================================
# SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
# SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
# SPDX-FileCopyrightText: 2022 Jessica Clarke <jrtc27@jrtc27.com>
#
# SPDX-License-Identifier: BSD-3-Clause
#=============================================================================

find_package(PkgConfig)
pkg_check_modules(PKG_libseat QUIET libseat)

set(Libseat_VERSION ${PKG_libseat_VERSION})

find_library(Libseat_LIBRARY NAMES seat HINTS ${PKG_Libseat_LIBRARY_DIRS})

find_package_handle_standard_args(Libseat
    FOUND_VAR Libseat_FOUND
    REQUIRED_VARS Libseat_LIBRARY
    VERSION_VAR Libseat_VERSION
)

mark_as_advanced(
    Libseat_VERSION
)
