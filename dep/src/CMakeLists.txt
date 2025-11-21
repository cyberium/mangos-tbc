#
# This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

include(FetchContent)
# avoid online check when already downloaded (little speed up)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

if(BUILD_EXTRACTORS AND NOT (BZip2_FOUND OR BZIP2_FOUND))
  add_subdirectory(bzip2)
endif()

# === zlib dependency resolution ===
find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND OR ZLIB_VERSION_STRING VERSION_LESS 1.3.1)
  message(VERBOSE "zlib: system version not found or too old (have: ${ZLIB_VERSION_STRING}, need ≥1.3.1) → fetching from source.")
  include(FetchContent)

  FetchContent_Declare(
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    GIT_TAG 5a82f71ed1dfc0bec044d9702463dbdf84ea3b71 # v1.3.1 in develop branch from 19/02/2025
    SYSTEM  # suppresses compiler warnings from zlib headers
  )

  set(SKIP_INSTALL_ALL ON CACHE INTERNAL "")
  set(ZLIB_ENABLE_TESTS OFF CACHE INTERNAL "")
  set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
  # set(ZLIB_COMPAT ON) # do we need this?

  FetchContent_MakeAvailable(zlib)

  get_target_property(_zlib_ver zlib VERSION)
  message(STATUS "zlib: using built version ${_zlib_ver}")

else()
  message(STATUS "zlib: using system version ${ZLIB_VERSION_STRING}")

  if(NOT TARGET zlib)
    add_library(zlib INTERFACE IMPORTED GLOBAL)
    if(TARGET ZLIB::ZLIB)
      set_property(TARGET zlib PROPERTY INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    else()
      set_property(TARGET zlib PROPERTY INTERFACE_LINK_LIBRARIES "${ZLIB_LIBRARIES}")
      set_property(TARGET zlib PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}")
    endif()
  endif()
endif()

if (BUILD_GAME_SERVER OR BUILD_LOGIN_SERVER)
  add_subdirectory(gsoap)
endif()

if (BUILD_GAME_SERVER OR BUILD_LOGIN_SERVER OR BUILD_EXTRACTORS)
  add_subdirectory(utf8cpp)
endif()
