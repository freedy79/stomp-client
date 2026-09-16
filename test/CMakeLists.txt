# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 freedy79

cmake_minimum_required(VERSION 3.20)

add_executable(test_stomp
    TestStomp.cpp
)

target_link_libraries(test_stomp
    PRIVATE
        stomp_client
)

add_test(NAME StompClientTests COMMAND test_stomp)
