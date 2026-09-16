cmake_minimum_required(VERSION 3.20)

add_executable(stomp_example_client
    main.cpp
)

target_link_libraries(stomp_example_client
    PRIVATE
        stomp_client
)
