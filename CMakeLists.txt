cmake_minimum_required (VERSION 3.1) # 3.1 adds C_STANDARD
project (pty LANGUAGES C)
add_executable (pty pty.c)
set_property (TARGET pty PROPERTY C_STANDARD 99)
target_link_libraries (pty -lutil)
install (TARGETS pty DESTINATION bin)
