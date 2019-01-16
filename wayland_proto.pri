isEmpty(WAYLAND_PROTOS):error("Define WAYLAND_PROTOS before including wayland_protocols.pri")

wayland_scanner_client.name = wayland-scanner headers
wayland_scanner_client.input = WAYLAND_PROTOS
wayland_scanner_client.output = ${QMAKE_FILE_BASE}.h
wayland_scanner_client.depends = ${QMAKE_FILE_IN}
wayland_scanner_client.commands = wayland-scanner client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_scanner_client.variable_out = HEADERS
QMAKE_EXTRA_COMPILERS += wayland_scanner_client

wayland_scanner_code.name = protobuf sources
wayland_scanner_code.input = WAYLAND_PROTOS
wayland_scanner_code.output = ${QMAKE_FILE_BASE}.c
wayland_scanner_client.depends = ${QMAKE_FILE_IN}
wayland_scanner_code.commands = wayland-scanner private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_scanner_code.variable_out = SOURCES
QMAKE_EXTRA_COMPILERS += wayland_scanner_code
