# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# cmake --build --preset release --target package

install(TARGETS moas-web-client RUNTIME DESTINATION .)
install(FILES
    README.md
    CHANGELOG.md
    LICENSE
    moas-web-client.example.json
    DESTINATION .)
install(FILES docs/user-guide.md DESTINATION docs)
install(FILES third_party/README.md DESTINATION third_party)
install(FILES third_party/cjson/LICENSE
    DESTINATION third_party RENAME LICENSE.cJSON)
install(FILES third_party/picohttpparser/LICENSE
    DESTINATION third_party RENAME LICENSE.picohttpparser)

string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" moas_platform)
set(CPACK_PACKAGE_FILE_NAME
    "${PROJECT_NAME}-${PROJECT_VERSION}-${moas_platform}")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
